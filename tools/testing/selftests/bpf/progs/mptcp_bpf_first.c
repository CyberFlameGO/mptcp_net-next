// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022, SUSE. */

#include <linux/bpf.h>
#include "bpf_mptcp_helpers.h"

char _license[] SEC("license") = "GPL";

SEC("struct_ops/mptcp_sched_first_init")
void BPF_PROG(mptcp_sched_first_init, struct mptcp_sock *msk)
{
}

SEC("struct_ops/mptcp_sched_first_release")
void BPF_PROG(mptcp_sched_first_release, struct mptcp_sock *msk)
{
}

struct sock *BPF_STRUCT_OPS(bpf_first_get_subflow, struct mptcp_sock *msk, bool retrans)
{
	struct sock *ssk = msk->first;

	msk->last_snd = ssk;
	msk->call_me_again = 0;
	return ssk;
}

SEC(".struct_ops")
struct mptcp_sched_ops first = {
	.init		= (void *)mptcp_sched_first_init,
	.release	= (void *)mptcp_sched_first_release,
	.get_subflow	= (void *)bpf_first_get_subflow,
	.name		= "bpf_first",
};
