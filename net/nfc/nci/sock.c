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
#include <linux/nfc.h>

#include "../nfc.h"
#include <net/nfc/nfc.h>
#include <net/nfc/nci_core.h>

//#include "nfc.h"

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

static int nci_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	pr_debug("sock=%p sk=%p\n", sock, sk);

	if (!sk)
		return 0;

	if (sk->sk_state == NCI_BOUND) {
//	        nfc_pda_close(nfc_pda_sock(sk)->dev);
//		nfc_put_device(nfc_pda_sock(sk)->dev);
//		nfc_pda_sock(sk)->dev = NULL;

		nci_sock_unlink(&nci_sk_list, sk);
	}

	sock_orphan(sk);
	sock_put(sk);

	return 0;
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
	.bind           = sock_no_bind,
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
