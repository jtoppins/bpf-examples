/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <net/if.h>
#include <linux/if_arp.h>

#include <bpf/libbpf.h>

#include "bond-active.h"
#include "pkt-loop-filter.h"
#include "pkt-loop-filter.kern.skel.h"

int get_bond_interfaces(int bond_ifindex, int *ifindexes, int *num_ifindexes)
{
	char sysfsname[100], buf[100], bond_ifname[IF_NAMESIZE];
	char *ifname, *tok;
	int fd, err;
	size_t len;

	if (!if_indextoname(bond_ifindex, bond_ifname))
		return -errno;

        snprintf(sysfsname, sizeof(sysfsname), "/sys/class/net/%s/bonding/slaves", bond_ifname);
        sysfsname[sizeof(sysfsname)-1] = '\0';

	fd = open(sysfsname, O_RDONLY);
	if (fd < 0)
		return fd;

	len = read(fd, buf, sizeof(buf));
	close(fd);
	if (len < 0)
		return len;
	buf[len] = '\0';

	tok = buf;
	while ((ifname = strtok(tok, " \n"))) {
		int ifindex;

		ifindex = if_nametoindex(ifname);
		if (!ifindex) {
			err = -errno;
			fprintf(stderr, "Couldn't get ifindex for iface '%s': %s\n",
				ifname, strerror(-err));
			return err;
		}

		ifindexes[*num_ifindexes] = ifindex;
		*num_ifindexes += 1;
		if (*num_ifindexes >= MAX_IFINDEXES) {
			fprintf(stderr, "Too many ifindexes in bond\n");
			return -E2BIG;
		}
		tok = NULL;
	}
	return 0;
}

int usage(const char *progname)
{
	fprintf(stderr, "Usage: %s <ifname> [--unload] [--debug]\n", progname);
	return 1;
}

int main(int argc, char *argv[])
{
	int err = 0, i, num_ifindexes = 0, _err, ingress_fd, egress_fd;
	int bond_ifindex = 0, active_ifindex, ifindexes[MAX_IFINDEXES];
	char pin_path[100], bond_ifname[IF_NAMESIZE];
	struct pkt_loop_filter_kern *skel = NULL;
	struct bpf_link *trace_link = NULL;
	bool unload = false, debug = false;
	__u64 netns_cookie;
	DECLARE_LIBBPF_OPTS(bpf_tc_hook, hook, .attach_point = BPF_TC_INGRESS | BPF_TC_EGRESS);

	if (argc < 2)
		return usage(argv[0]);

	for (i = 0; i < argc - 1; i++) {
		char *ifname = argv[i+1];

		if (!strcmp(ifname, "--unload")) {
			unload = true;
			continue;
		}

		if (!strcmp(ifname, "--debug")) {
			debug = true;
			continue;
		}

		if (bond_ifindex)
			return usage(argv[0]);

		bond_ifindex = if_nametoindex(ifname);
		if (!bond_ifindex) {
			fprintf(stderr, "Couldn't find interface '%s'\n", ifname);
			return 1;
		}
	}

	if (!bond_ifindex) {
		fprintf(stderr, "Missing interface name\n");
		return 1;
	}
	if (!if_indextoname(bond_ifindex, bond_ifname)) {
		err = -errno;
		perror("if_indextoname");
		return err;
	}

	snprintf(pin_path, sizeof(pin_path), "/sys/fs/bpf/pkt-loop-filter-%d",
		 bond_ifindex);
	pin_path[sizeof(pin_path) - 1] = '\0';

	err = get_bond_interfaces(bond_ifindex, ifindexes, &num_ifindexes);
	if (err) {
		fprintf(stderr, "Unable to get bond interfaces: %s\n",
			strerror(-err));
		return err;
	}

	if (unload)
		goto unload;

	active_ifindex = get_bond_active_ifindex(bond_ifindex);
	if (active_ifindex < 0) {
		fprintf(stderr, "Unable to get active index for bond %s: %s\n",
			bond_ifname, strerror(-active_ifindex));
		return active_ifindex;
	}

	err = get_netns_cookie(&netns_cookie);
	if (err)
		return err;

	printf("%s: Found %d ifaces, active idx %d\n",
	       bond_ifname, num_ifindexes, active_ifindex);

	skel = pkt_loop_filter_kern__open();
	err = libbpf_get_error(skel);
	if (err) {
		fprintf(stderr, "Couldn't open BPF skeleton: %s\n", strerror(errno));
		return err;
	}

	err = bpf_map__set_max_entries(skel->maps.iface_state, 1024);
	if (err) {
		fprintf(stderr, "Failed to set map size\n");
		goto out;
	}

	/* Propagate active ifindexes to the BPF program global variables so the
	 * BPF program can use it to filter multicast traffic
	 */
	skel->bss->active_ifindex = active_ifindex;
	skel->bss->bond_ifindex = bond_ifindex;

	/* enable debug flag if set on command line */
	skel->rodata->debug_output = debug;
	skel->rodata->netns_cookie = netns_cookie;

	err = pkt_loop_filter_kern__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load object\n");
		goto out;
	}

	egress_fd = bpf_program__fd(skel->progs.record_egress_pkt);
	if (egress_fd < 0) {
		fprintf(stderr, "Couldn't find program 'record_egress_pkt'\n");
		err = -ENOENT;
		goto out;
	}

	ingress_fd = bpf_program__fd(skel->progs.filter_ingress_pkt);
	if (ingress_fd < 0) {
		fprintf(stderr, "Couldn't find program 'filter_ingress_pkt'\n");
		err = -ENOENT;
		goto out;
	}

	for (i = 0; i < num_ifindexes; i++) {
		DECLARE_LIBBPF_OPTS(bpf_tc_opts, attach_egress,
				    .prog_fd = egress_fd);
		DECLARE_LIBBPF_OPTS(bpf_tc_opts, attach_ingress,
				    .prog_fd = ingress_fd);
		char ifname[IF_NAMESIZE];

		if (!if_indextoname(ifindexes[i], ifname)) {
			err = -errno;
			fprintf(stderr, "Couldn't get ifname for ifindex %d: %s\n", ifindexes[i], strerror(-err));
			goto out;
		}

		hook.ifindex = ifindexes[i];
		hook.attach_point = BPF_TC_EGRESS | BPF_TC_INGRESS;
		err = bpf_tc_hook_create(&hook);
		if (err && err != -EEXIST) {
			fprintf(stderr, "Couldn't create egress hook for interface %s\n", ifname);
			goto unload;
		}

		hook.attach_point = BPF_TC_EGRESS;
		err = bpf_tc_attach(&hook, &attach_egress);
		if (err) {
			fprintf(stderr, "Couldn't attach egress program to interface %s: %s\n", ifname, strerror(errno));
			goto unload;
		}

		hook.attach_point = BPF_TC_INGRESS;
		err = bpf_tc_attach(&hook, &attach_ingress);
		if (err) {
			fprintf(stderr, "Couldn't attach ingress program to interface %s: %s\n", ifname, strerror(errno));
			goto unload;
		}
	}

	trace_link = bpf_program__attach(skel->progs.handle_change_slave);
	if (!trace_link) {
		fprintf(stderr, "Couldn't attach tracing prog: %s\n", strerror(errno));
		err = -EFAULT;
		goto unload;
	}

	err = bpf_link__pin(trace_link, pin_path);
	if (err) {
		fprintf(stderr, "Couldn't pin bpf_link: %s\n", strerror(errno));
		goto unload;
	}

out:
	bpf_link__destroy(trace_link);
	pkt_loop_filter_kern__destroy(skel);
	return err;

unload:
	for (i = 0; i < num_ifindexes; i++) {
		char ifname[IF_NAMESIZE];

		hook.ifindex = ifindexes[i];
		hook.attach_point = BPF_TC_EGRESS | BPF_TC_INGRESS;
		_err = bpf_tc_hook_destroy(&hook);
		if (_err) {
			fprintf(stderr, "Couldn't remove clsact qdisc on %s\n",
				if_indextoname(ifindexes[i], ifname));
			err = _err;
		}

	}
	unlink(pin_path);
	goto out;
}
