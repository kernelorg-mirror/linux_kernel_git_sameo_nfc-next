/*
 * NCI physical access sockets.
 *
 * Copyright (c) 2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#define pr_fmt(fmt) "nci_phy: %s: " fmt, __func__

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rfkill.h>
#include <linux/nfc.h>

#include "../nfc.h"
#include <net/nfc/nfc.h>
#include <net/nfc/nci_core.h>

#define nfc_nci_sock(sk) ((struct nfc_nci_sock *) (sk))

static struct nfc_sock_list nci_sk_list = {
	.lock = __RW_LOCK_UNLOCKED(nci_sk_list.lock)
};

/* TODO Factorize with rawsock */
static void nci_sock_link(struct nfc_sock_list *l, struct sock *sk)
{
	write_lock(&l->lock);
	sk_add_node(sk, &l->head);
	write_unlock(&l->lock);
}

static void nci_sock_unlink(struct nfc_sock_list *l, struct sock *sk)
{
	write_lock(&l->lock);
	sk_del_node_init(sk);
	write_unlock(&l->lock);
}

static int nci_sock_close(struct nfc_dev *dev)
{
	struct nci_dev *ndev = nfc_get_drvdata(dev);

	if (!test_and_clear_bit(NCI_UP, &ndev->flags))
		return 0;

	ndev->ops->close(ndev);

	/* Clear flags */
	ndev->flags = 0;

	return 0;
}

static int nci_sock_open(struct nfc_dev *dev)
{
	struct nci_dev *ndev = nfc_get_drvdata(dev);
	int rc = 0;

	device_lock(&dev->dev);

	if (dev->rfkill && rfkill_blocked(dev->rfkill)) {
		rc = -ERFKILL;
		goto error;
	}

	if (!device_is_registered(&dev->dev)) {
		rc = -ENODEV;
		goto error;
	}

	if (test_bit(NCI_UP, &ndev->flags)) {
		rc = -EBUSY;
		goto error;
	}

	if (ndev->ops->open(ndev)) {
		rc = -EIO;
		goto error;
	}

	if (!rc) {
		set_bit(NCI_UP, &ndev->flags);
		atomic_set(&ndev->state, NCI_IDLE);
	} else {
		/* Open failed, cleanup */
		ndev->ops->close(ndev);
		ndev->flags = 0;
	}

error:
	device_unlock(&dev->dev);
	return rc;
}

static int nci_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct nfc_nci_sock *nci_sock = nfc_nci_sock(sk);

	pr_debug("sock=%p sk=%p\n", sock, sk);

	if (!sk)
		return 0;

	if (sk->sk_state == NCI_BOUND) {
		nci_sock_close(nci_sock->dev);
		nfc_put_device(nci_sock->dev);
		nci_sock->dev = NULL;

		nci_sock_unlink(&nci_sk_list, sk);
	}

	sock_orphan(sk);
	sock_put(sk);

	return 0;
}

static int nci_sock_bind(struct socket *sock, struct sockaddr *addr, int alen)
{
	struct sock *sk = sock->sk;
	struct sockaddr_nfc_nci nci_addr;
	struct nfc_nci_sock *nci_sock = nfc_nci_sock(sk);
	struct nfc_dev *dev;
	int len, ret = 0;

	if (!addr || addr->sa_family != AF_NFC)
		return -EINVAL;

	pr_debug("sk %p addr %p family %d\n", sk, addr, addr->sa_family);

	memset(&nci_addr, 0, sizeof(nci_addr));
	len = min_t(unsigned int, sizeof(nci_addr), alen);
	memcpy(&nci_addr, addr, len);

	lock_sock(sk);

	if (sk->sk_state != NCI_CLOSED) {
		ret = -EBADFD;
		goto error;
	}

	if (!capable(CAP_NET_ADMIN)) {
		ret = -EPERM;
		goto error;
	}

	dev = nfc_get_device(nci_addr.dev_idx);
	if (dev == NULL) {
		ret = -ENODEV;
		goto error;
	}

	ret = nci_sock_open(dev);
	if (ret)
		goto put_dev;

	nci_sock->dev = dev;
	nci_sock_link(&nci_sk_list, sk);

	pr_debug("NCI Socket bound to nfc%d\n", nci_addr.dev_idx);

	sk->sk_state = NCI_BOUND;

put_dev:
	nfc_put_device(dev);
error:
	release_sock(sk);
	return ret;
}

static struct proto nci_sock_proto = {
	.name     = "NFC_NCI",
	.owner    = THIS_MODULE,
	.obj_size = sizeof(struct nfc_nci_sock),
};

static const struct proto_ops nci_sock_ops = {
	.family         = PF_NFC,
	.owner          = THIS_MODULE,
	.release        = nci_sock_release,
	.bind           = nci_sock_bind,
	.connect        = sock_no_connect,
	.socketpair     = sock_no_socketpair,
	.accept         = sock_no_accept,
	.getname        = sock_no_getname,
	.poll           = datagram_poll,
	.ioctl          = sock_no_ioctl,
	.listen         = sock_no_listen,
	.shutdown       = sock_no_shutdown,
	.setsockopt     = sock_no_setsockopt,
	.getsockopt     = sock_no_getsockopt,
	.sendmsg        = sock_no_sendmsg,
	.recvmsg        = sock_no_recvmsg,
	.mmap           = sock_no_mmap,
};

static int nci_sock_create(struct net *net, struct socket *sock,
			    const struct nfc_protocol *nfc_proto, int kern)
{
	struct sock *sk;

	pr_debug("%p\n", sock);

	if (sock->type != SOCK_RAW)
		return -ESOCKTNOSUPPORT;

	sock->ops = &nci_sock_ops;

	sk = sk_alloc(net, PF_NFC, GFP_ATOMIC, &nci_sock_proto, kern);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);
	sock_reset_flag(sk, SOCK_ZAPPED);
	sock->state = SS_UNCONNECTED;

	sk->sk_protocol = NFC_SOCKPROTO_NCI;
	sk->sk_state = NCI_CLOSED;

	return 0;
}

static const struct nfc_protocol nci_nfc_proto = {
	.id	  = NFC_SOCKPROTO_NCI,
	.proto    = &nci_sock_proto,
	.owner    = THIS_MODULE,
	.create   = nci_sock_create
};

int __init nfc_nci_sock_init(void)
{
	return nfc_proto_register(&nci_nfc_proto);
}

void nfc_nci_sock_exit(void)
{
	nfc_proto_unregister(&nci_nfc_proto);
}
