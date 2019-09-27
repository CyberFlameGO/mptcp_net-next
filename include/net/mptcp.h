/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Multipath TCP
 *
 * Copyright (c) 2017 - 2019, Intel Corporation.
 */

#ifndef __NET_MPTCP_H
#define __NET_MPTCP_H

struct mptcp_out_options {
#if IS_ENABLED(CONFIG_MPTCP)
	u16 suboptions;
	u64 sndr_key;
	u64 rcvr_key;
#endif
};

#ifdef CONFIG_MPTCP

void mptcp_init(void);

void mptcp_parse_option(const unsigned char *ptr, int opsize,
			struct tcp_options_received *opt_rx);
void mptcp_write_options(__be32 *ptr, struct mptcp_out_options *opts);

#else

static inline void mptcp_init(void)
{
}

static inline void mptcp_parse_option(const unsigned char *ptr, int opsize,
				      struct tcp_options_received *opt_rx)
{
}

#endif /* CONFIG_MPTCP */
#endif /* __NET_MPTCP_H */
