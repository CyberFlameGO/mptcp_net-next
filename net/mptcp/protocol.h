/* SPDX-License-Identifier: GPL-2.0 */
/* Multipath TCP
 *
 * Copyright (c) 2017 - 2019, Intel Corporation.
 */

#ifndef __MPTCP_PROTOCOL_H
#define __MPTCP_PROTOCOL_H

#include <linux/random.h>
#include <linux/spinlock.h>

/* MPTCP option bits */
#define OPTION_MPTCP_MPC_SYN	BIT(0)
#define OPTION_MPTCP_MPC_SYNACK	BIT(1)
#define OPTION_MPTCP_MPC_ACK	BIT(2)

/* MPTCP option subtypes */
#define MPTCPOPT_MP_CAPABLE	0
#define MPTCPOPT_MP_JOIN	1
#define MPTCPOPT_DSS		2
#define MPTCPOPT_ADD_ADDR	3
#define MPTCPOPT_RM_ADDR	4
#define MPTCPOPT_MP_PRIO	5
#define MPTCPOPT_MP_FAIL	6
#define MPTCPOPT_MP_FASTCLOSE	7

/* MPTCP suboption lengths */
#define TCPOLEN_MPTCP_MPC_SYN		12
#define TCPOLEN_MPTCP_MPC_SYNACK	20
#define TCPOLEN_MPTCP_MPC_ACK		20
#define TCPOLEN_MPTCP_DSS_BASE		4
#define TCPOLEN_MPTCP_DSS_ACK32		4
#define TCPOLEN_MPTCP_DSS_ACK64		8
#define TCPOLEN_MPTCP_DSS_MAP32		10
#define TCPOLEN_MPTCP_DSS_MAP64		14
#define TCPOLEN_MPTCP_DSS_CHECKSUM	2

/* MPTCP MP_CAPABLE flags */
#define MPTCP_VERSION_MASK	(0x0F)
#define MPTCP_CAP_CHECKSUM_REQD	BIT(7)
#define MPTCP_CAP_EXTENSIBILITY	BIT(6)
#define MPTCP_CAP_HMAC_SHA1	BIT(0)
#define MPTCP_CAP_FLAG_MASK	(0x3F)

/* MPTCP DSS flags */
#define MPTCP_DSS_DATA_FIN	BIT(4)
#define MPTCP_DSS_DSN64		BIT(3)
#define MPTCP_DSS_HAS_MAP	BIT(2)
#define MPTCP_DSS_ACK64		BIT(1)
#define MPTCP_DSS_HAS_ACK	BIT(0)
#define MPTCP_DSS_FLAG_MASK	(0x1F)

struct mptcp_pm_data {
	u8	local_valid;
	u8	local_id;
	sa_family_t local_family;
	union {
		struct in_addr local_addr;
#if IS_ENABLED(CONFIG_IPV6)
		struct in6_addr local_addr6;
#endif
	};
	u8	remote_valid;
	u8	remote_id;
	sa_family_t remote_family;
	union {
		struct in_addr remote_addr;
#if IS_ENABLED(CONFIG_IPV6)
		struct in6_addr remote_addr6;
#endif
	};
	u8	server_side : 1,
		fully_established : 1;

	/* for interim path manager */
	struct	work_struct addr_work;
	struct	work_struct subflow_work;
	u32	token;
};

/* MPTCP connection sock */
struct mptcp_sock {
	/* inet_connection_sock must be the first member */
	struct inet_connection_sock sk;
	u64		local_key;
	u64		remote_key;
	u64		write_seq;
	u64		ack_seq;
	u32		token;
	struct list_head conn_list;
	struct socket	*subflow; /* outgoing connect/listener/!mp_capable */
	struct mptcp_pm_data	pm;
};

#define mptcp_for_each_subflow(__msk, __subflow)			\
	list_for_each_entry(__subflow, &((__msk)->conn_list), node)

static inline struct mptcp_sock *mptcp_sk(const struct sock *sk)
{
	return (struct mptcp_sock *)sk;
}

struct subflow_request_sock {
	struct	tcp_request_sock sk;
	u8	mp_capable : 1,
		mp_join : 1,
		checksum : 1,
		backup : 1,
		version : 4;
	u64	local_key;
	u64	remote_key;
	u64	idsn;
	u32	token;
	u32	ssn_offset;
};

static inline
struct subflow_request_sock *subflow_rsk(const struct request_sock *rsk)
{
	return (struct subflow_request_sock *)rsk;
}

/* MPTCP subflow context */
struct subflow_context {
	struct	list_head node;/* conn_list of subflows */
	u64	local_key;
	u64	remote_key;
	u32	token;
	u32     rel_write_seq;
	u64     idsn;
	u64	map_seq;
	u32	map_subflow_seq;
	u32	ssn_offset;
	u16	map_data_len;
	u16	request_mptcp : 1,  /* send MP_CAPABLE */
		request_cksum : 1,
		request_version : 4,
		mp_capable : 1,	    /* remote is MPTCP capable */
		fourth_ack : 1,     /* send initial DSS */
		conn_finished : 1,
		use_checksum : 1,
		map_valid : 1;

	struct  socket *tcp_sock;  /* underlying tcp_sock */
	struct  sock *conn;        /* parent mptcp_sock */
	void	(*tcp_sk_data_ready)(struct sock *sk);
};

static inline struct subflow_context *subflow_ctx(const struct sock *sk)
{
	struct inet_connection_sock *icsk = inet_csk(sk);

	return (struct subflow_context *)icsk->icsk_ulp_data;
}

static inline struct socket *
mptcp_subflow_tcp_socket(const struct subflow_context *subflow)
{
	return subflow->tcp_sock;
}

void subflow_init(void);
int subflow_create_socket(struct sock *sk, struct socket **new_sock);

extern const struct inet_connection_sock_af_ops ipv4_specific;

void mptcp_get_options(const struct sk_buff *skb,
		       struct tcp_options_received *opt_rx);

void mptcp_finish_connect(struct sock *sk, int mp_capable);

void token_new_request(struct request_sock *req, const struct sk_buff *skb);
void token_destroy_request(u32 token);
void token_new_connect(struct sock *sk);
void token_new_accept(struct sock *sk);
void token_update_accept(struct sock *sk, struct sock *conn);
void token_destroy(u32 token);

void crypto_key_sha1(u64 key, u32 *token, u64 *idsn);
static inline void crypto_key_gen_sha1(u64 *key, u32 *token, u64 *idsn)
{
	/* we might consider a faster version that computes the key as a
	 * hash of some information available in the MPTCP socket. Use
	 * random data at the moment, as it's probably the safest option
	 * in case multiple sockets are opened in different namespaces at
	 * the same time.
	 */
	get_random_bytes(key, sizeof(u64));
	crypto_key_sha1(*key, token, idsn);
}
void crypto_hmac_sha1(u64 key1, u64 key2, u32 nonce1, u32 nonce2,
		      u32 *hash_out);

void pm_init(void);
void pm_new_connection(struct mptcp_sock *msk, int server_side);
void pm_fully_established(struct mptcp_sock *msk);
void pm_connection_closed(struct mptcp_sock *msk);
void pm_subflow_established(struct mptcp_sock *msk, u8 id);
void pm_subflow_closed(struct mptcp_sock *msk, u8 id);
void pm_add_addr(struct mptcp_sock *msk, const struct in_addr *addr, u8 id);
void pm_add_addr6(struct mptcp_sock *msk, const struct in6_addr *addr, u8 id);
void pm_rm_addr(struct mptcp_sock *msk, u8 id);
int pm_addr_signal(struct mptcp_sock *msk, u8 *id,
		   struct sockaddr_storage *saddr);
int pm_get_local_id(struct request_sock *req, struct sock *sk,
		    const struct sk_buff *skb);

static inline struct mptcp_ext *mptcp_get_ext(struct sk_buff *skb)
{
	return (struct mptcp_ext *)skb_ext_find(skb, SKB_EXT_MPTCP);
}

#endif /* __MPTCP_PROTOCOL_H */
