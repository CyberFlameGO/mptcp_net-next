// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Tessares SA. */

#include <test_progs.h>
#include "cgroup_helpers.h"
#include "network_helpers.h"
#include "bpf_first.skel.h"

#define min(a, b) ((a) < (b) ? (a) : (b))

#ifndef TCP_CA_NAME_MAX
#define TCP_CA_NAME_MAX	16
#endif

struct mptcp_storage {
	__u32 invoked;
	__u32 is_mptcp;
	struct sock *sk;
	__u32 token;
	struct sock *first;
	char ca_name[TCP_CA_NAME_MAX];
};

static char monitor_log_path[64];
static const unsigned int total_bytes = 10 * 1024 * 1024;
static int stop, duration;

static int verify_tsk(int map_fd, int client_fd)
{
	char *msg = "plain TCP socket";
	int err = 0, cfd = client_fd;
	struct mptcp_storage val;

	if (CHECK_FAIL(bpf_map_lookup_elem(map_fd, &cfd, &val) < 0)) {
		perror("Failed to read socket storage");
		return -1;
	}

	if (val.invoked != 1) {
		log_err("%s: unexpected invoked count %d != 1",
			msg, val.invoked);
		err++;
	}

	if (val.is_mptcp != 0) {
		log_err("%s: unexpected bpf_tcp_sock.is_mptcp %d != 0",
			msg, val.is_mptcp);
		err++;
	}

	return err;
}

/*
 * Parse the token from the output of 'ip mptcp monitor':
 *
 * [       CREATED] token=3ca933d3 remid=0 locid=0 saddr4=127.0.0.1 ...
 * [       CREATED] token=2ab57040 remid=0 locid=0 saddr4=127.0.0.1 ...
 */
static __u32 get_msk_token(void)
{
	char *prefix = "[       CREATED] token=";
	char buf[BUFSIZ] = {};
	__u32 token = 0;
	ssize_t len;
	int fd;

	sync();

	fd = open(monitor_log_path, O_RDONLY);
	if (CHECK_FAIL(fd < 0)) {
		log_err("Failed to open %s", monitor_log_path);
		return token;
	}

	len = read(fd, buf, sizeof(buf));
	if (CHECK_FAIL(len < 0)) {
		log_err("Failed to read %s", monitor_log_path);
		goto err;
	}

	if (strncmp(buf, prefix, strlen(prefix))) {
		log_err("Invalid prefix %s", buf);
		goto err;
	}

	token = strtol(buf + strlen(prefix), NULL, 16);

err:
	close(fd);
	return token;
}

void get_msk_ca_name(char ca_name[])
{
	FILE *stream = popen("sysctl -b net.ipv4.tcp_congestion_control", "r");

	if (!fgets(ca_name, TCP_CA_NAME_MAX, stream))
		log_err("Failed to read ca_name");

	pclose(stream);
}

static int verify_msk(int map_fd, int client_fd)
{
	char *msg = "MPTCP subflow socket";
	int err = 0, cfd = client_fd;
	struct mptcp_storage val;
	char ca_name[TCP_CA_NAME_MAX];
	__u32 token;

	token = get_msk_token();
	if (token <= 0) {
		log_err("Unexpected token %x", token);
		return -1;
	}

	get_msk_ca_name(ca_name);

	if (CHECK_FAIL(bpf_map_lookup_elem(map_fd, &cfd, &val) < 0)) {
		perror("Failed to read socket storage");
		return -1;
	}

	if (val.invoked != 1) {
		log_err("%s: unexpected invoked count %d != 1",
			msg, val.invoked);
		err++;
	}

	if (val.is_mptcp != 1) {
		log_err("%s: unexpected bpf_tcp_sock.is_mptcp %d != 1",
			msg, val.is_mptcp);
		err++;
	}

	if (val.token != token) {
		log_err("Unexpected mptcp_sock.token %x != %x",
			val.token, token);
		err++;
	}

	if (val.first != val.sk) {
		log_err("Unexpected mptcp_sock.first %p != %p",
			val.first, val.sk);
		err++;
	}

	if (strncmp(val.ca_name, ca_name, TCP_CA_NAME_MAX)) {
		log_err("Unexpected mptcp_sock.ca_name %s != %s",
			val.ca_name, ca_name);
		err++;
	}

	return err;
}

static int run_test(int cgroup_fd, int server_fd, bool is_mptcp)
{
	int client_fd, prog_fd, map_fd, err;
	struct bpf_program *prog;
	struct bpf_object *obj;
	struct bpf_map *map;

	obj = bpf_object__open("./mptcp_sock.o");
	if (libbpf_get_error(obj))
		return -EIO;

	err = bpf_object__load(obj);
	if (CHECK_FAIL(err))
		goto out;

	prog = bpf_object__find_program_by_name(obj, "_sockops");
	if (CHECK_FAIL(!prog)) {
		err = -EIO;
		goto out;
	}

	prog_fd = bpf_program__fd(prog);
	if (CHECK_FAIL(prog_fd < 0)) {
		err = -EIO;
		goto out;
	}

	map = bpf_object__find_map_by_name(obj, "socket_storage_map");
	if (CHECK_FAIL(!map)) {
		err = -EIO;
		goto out;
	}

	map_fd = bpf_map__fd(map);
	if (CHECK_FAIL(map_fd < 0)) {
		err = -EIO;
		goto out;
	}

	err = bpf_prog_attach(prog_fd, cgroup_fd, BPF_CGROUP_SOCK_OPS, 0);
	if (CHECK_FAIL(err))
		goto out;

	client_fd = is_mptcp ? connect_to_mptcp_fd(server_fd, 0) :
			       connect_to_fd(server_fd, 0);
	if (client_fd < 0) {
		err = -EIO;
		goto out;
	}

	err += is_mptcp ? verify_msk(map_fd, client_fd) :
			  verify_tsk(map_fd, client_fd);

	close(client_fd);

out:
	bpf_object__close(obj);
	return err;
}

void test_base(void)
{
	char cmd[256], tmp_dir[] = "/tmp/XXXXXX";
	int server_fd, cgroup_fd;

	cgroup_fd = test__join_cgroup("/mptcp");
	if (CHECK_FAIL(cgroup_fd < 0))
		return;

	/* without MPTCP */
	server_fd = start_server(AF_INET, SOCK_STREAM, NULL, 0, 0);
	if (CHECK_FAIL(server_fd < 0))
		goto with_mptcp;

	CHECK_FAIL(run_test(cgroup_fd, server_fd, false));

	close(server_fd);

with_mptcp:
	/* with MPTCP */
	if (CHECK_FAIL(!mkdtemp(tmp_dir)))
		goto close_cgroup_fd;
	snprintf(monitor_log_path, sizeof(monitor_log_path),
		 "%s/ip_mptcp_monitor", tmp_dir);
	snprintf(cmd, sizeof(cmd), "ip mptcp monitor > %s &", monitor_log_path);
	if (CHECK_FAIL(system(cmd)))
		goto close_cgroup_fd;
	server_fd = start_mptcp_server(AF_INET, NULL, 0, 0);
	if (CHECK_FAIL(server_fd < 0))
		goto close_cgroup_fd;

	CHECK_FAIL(run_test(cgroup_fd, server_fd, true));

	close(server_fd);
	snprintf(cmd, sizeof(cmd), "rm -rf %s", tmp_dir);
	system(cmd);

close_cgroup_fd:
	close(cgroup_fd);
}

static void *server(void *arg)
{
	int lfd = (int)(long)arg, err = 0, fd;
	ssize_t nr_sent = 0, bytes = 0;
	char batch[1500];

	fd = accept(lfd, NULL, NULL);
	while (fd == -1) {
		if (errno == EINTR)
			continue;
		err = -errno;
		goto done;
	}

	if (settimeo(fd, 0)) {
		err = -errno;
		goto done;
	}

	while (bytes < total_bytes && !READ_ONCE(stop)) {
		nr_sent = send(fd, &batch,
			       min(total_bytes - bytes, sizeof(batch)), 0);
		if (nr_sent == -1 && errno == EINTR)
			continue;
		if (nr_sent == -1) {
			err = -errno;
			break;
		}
		bytes += nr_sent;
	}

	CHECK(bytes != total_bytes, "send", "%zd != %u nr_sent:%zd errno:%d\n",
	      bytes, total_bytes, nr_sent, errno);

done:
	if (fd >= 0)
		close(fd);
	if (err) {
		WRITE_ONCE(stop, 1);
		return ERR_PTR(err);
	}
	return NULL;
}

static void send_data(int lfd, int fd)
{
	ssize_t nr_recv = 0, bytes = 0;
	pthread_t srv_thread;
	void *thread_ret;
	char batch[1500];
	int err;

	WRITE_ONCE(stop, 0);

	err = pthread_create(&srv_thread, NULL, server, (void *)(long)lfd);
	if (CHECK(err != 0, "pthread_create", "err:%d errno:%d\n", err, errno))
		return;

	/* recv total_bytes */
	while (bytes < total_bytes && !READ_ONCE(stop)) {
		nr_recv = recv(fd, &batch,
			       min(total_bytes - bytes, sizeof(batch)), 0);
		if (nr_recv == -1 && errno == EINTR)
			continue;
		if (nr_recv == -1)
			break;
		bytes += nr_recv;
	}

	CHECK(bytes != total_bytes, "recv", "%zd != %u nr_recv:%zd errno:%d\n",
	      bytes, total_bytes, nr_recv, errno);

	WRITE_ONCE(stop, 1);

	pthread_join(srv_thread, &thread_ret);
	CHECK(IS_ERR(thread_ret), "pthread_join", "thread_ret:%ld",
	      PTR_ERR(thread_ret));
}

static void test_first(void)
{
	struct bpf_first *first_skel;
	int server_fd, client_fd;
	struct bpf_link *link;

	first_skel = bpf_first__open_and_load();
	if (CHECK(!first_skel, "bpf_first__open_and_load", "failed\n"))
		return;

	link = bpf_map__attach_struct_ops(first_skel->maps.first);
	if (!ASSERT_OK_PTR(link, "bpf_map__attach_struct_ops")) {
		bpf_first__destroy(first_skel);
		return;
	}

	system("sysctl -q net.mptcp.scheduler=bpf_first");
	server_fd = start_mptcp_server(AF_INET, NULL, 0, 0);
	client_fd = connect_to_mptcp_fd(server_fd, 0);

	send_data(server_fd, client_fd);

	close(client_fd);
	close(server_fd);
	bpf_link__destroy(link);
	bpf_first__destroy(first_skel);
}

void test_mptcp(void)
{
	if (test__start_subtest("base"))
		test_base();
	if (test__start_subtest("first"))
		test_first();
}
