// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022, SUSE. */

#include <linux/bpf.h>
#include "bpf_tcp_helpers.h"

char _license[] SEC("license") = "GPL";

SEC("struct_ops/mptcp_sched_bkup_init")
void BPF_PROG(mptcp_sched_bkup_init, const struct mptcp_sock *msk)
{
}

SEC("struct_ops/mptcp_sched_bkup_release")
void BPF_PROG(mptcp_sched_bkup_release, const struct mptcp_sock *msk)
{
}

void BPF_STRUCT_OPS(bpf_bkup_get_subflow, const struct mptcp_sock *msk,
		    struct mptcp_sched_data *data)
{
	int nr = 0;

	for (int i = 0; i < MPTCP_SUBFLOWS_MAX; i++) {
		if (!data->contexts[i])
			break;

		if (!BPF_CORE_READ_BITFIELD_PROBED(data->contexts[i], backup)) {
			nr = i;
			break;
		}
	}

	mptcp_subflow_set_scheduled(data->contexts[nr], true);
}

SEC(".struct_ops")
struct mptcp_sched_ops bkup = {
	.init		= (void *)mptcp_sched_bkup_init,
	.release	= (void *)mptcp_sched_bkup_release,
	.get_subflow	= (void *)bpf_bkup_get_subflow,
	.name		= "bpf_bkup",
};
