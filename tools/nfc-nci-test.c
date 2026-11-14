// SPDX-License-Identifier: GPL-2.0
/*
 * nfc-nci-test — drive the NFC controller through the kernel NFC
 * generic-netlink interface.
 *
 *   nfc-nci-test up      NFC_CMD_DEV_UP  (nci_open_device: VEN on, CORE_RESET
 *                        over I2C — proves power, clock, I2C and IRQ)
 *   nfc-nci-test down    NFC_CMD_DEV_DOWN
 *   nfc-nci-test poll    DEV_UP + START_POLL and print discovered targets
 *
 * The rfkill soft block has to be cleared first:
 *   echo 0 > /sys/class/rfkill/rfkill0/soft
 */
#include <dirent.h>
#include <errno.h>
#include <linux/nfc.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define NFC_GENL_NAME "nfc"

#define NLA_ALIGNTO	4
#define NLA_ALIGN(len)	(((len) + NLA_ALIGNTO - 1) & ~(NLA_ALIGNTO - 1))
#define NLA_OK(nla, len) \
	((len) >= (int)sizeof(struct nlattr) && \
	 (nla)->nla_len >= sizeof(struct nlattr) && \
	 (nla)->nla_len <= (len))
#define NLA_NEXT(nla, len) \
	((len) -= NLA_ALIGN((nla)->nla_len), \
	 (struct nlattr *)(((char *)(nla)) + NLA_ALIGN((nla)->nla_len)))

static int nl_open(void)
{
	struct sockaddr_nl addr = { .nl_family = AF_NETLINK };
	int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);

	if (fd < 0)
		return -1;
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int genl_family_id(int fd)
{
	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
		char attrs[64];
	} req;
	struct sockaddr_nl addr = { .nl_family = AF_NETLINK };
	struct nlmsghdr *nlh;
	char buf[4096];
	ssize_t r;
	int id = -1;

	memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	req.n.nlmsg_type = GENL_ID_CTRL;
	req.n.nlmsg_flags = NLM_F_REQUEST;
	req.n.nlmsg_seq = 1;
	req.g.cmd = CTRL_CMD_GETFAMILY;
	req.g.version = 1;
	{
		struct nlattr *na = (struct nlattr *)((char *)&req +
						NLMSG_ALIGN(req.n.nlmsg_len));

		na->nla_type = CTRL_ATTR_FAMILY_NAME;
		na->nla_len = NLA_HDRLEN + sizeof(NFC_GENL_NAME);
		memcpy((char *)na + NLA_HDRLEN, NFC_GENL_NAME,
		       sizeof(NFC_GENL_NAME));
		req.n.nlmsg_len = NLMSG_ALIGN(req.n.nlmsg_len) +
				  NLA_ALIGN(na->nla_len);
	}
	if (sendto(fd, &req, req.n.nlmsg_len, 0, (struct sockaddr *)&addr,
		   sizeof(addr)) < 0)
		return -1;
	r = recv(fd, buf, sizeof(buf), 0);
	if (r < 0)
		return -1;

	for (nlh = (struct nlmsghdr *)buf; NLMSG_OK(nlh, (unsigned int)r);
	     nlh = NLMSG_NEXT(nlh, r)) {
		struct genlmsghdr *gh = NLMSG_DATA(nlh);
		int len = nlh->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
		struct nlattr *na = (struct nlattr *)((char *)gh + GENL_HDRLEN);

		for (; NLA_OK(na, len); na = NLA_NEXT(na, len))
			if (na->nla_type == CTRL_ATTR_FAMILY_ID)
				id = *(unsigned short *)((char *)na + NLA_HDRLEN);
	}
	return id;
}

/*
 * Join the family's multicast group.
 *
 * Without this the socket only ever sees ACKs: NFC_EVENT_TARGETS_FOUND is a
 * multicast notification, so poll() used to sit on a target that the kernel
 * had already reported (visible only in dmesg as "nfc_targets_found").
 */
static int genl_join_mcast(int fd, const char *family, const char *group)
{
	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
		char attrs[64];
	} req;
	struct sockaddr_nl addr = { .nl_family = AF_NETLINK };
	struct nlmsghdr *nlh;
	char buf[8192];
	ssize_t r;
	int gid = -1;

	memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	req.n.nlmsg_type = GENL_ID_CTRL;
	req.n.nlmsg_flags = NLM_F_REQUEST;
	req.n.nlmsg_seq = 2;
	req.g.cmd = CTRL_CMD_GETFAMILY;
	req.g.version = 1;
	{
		struct nlattr *na = (struct nlattr *)((char *)&req +
						NLMSG_ALIGN(req.n.nlmsg_len));

		na->nla_type = CTRL_ATTR_FAMILY_NAME;
		na->nla_len = NLA_HDRLEN + strlen(family) + 1;
		memcpy((char *)na + NLA_HDRLEN, family, strlen(family) + 1);
		req.n.nlmsg_len = NLMSG_ALIGN(req.n.nlmsg_len) +
				  NLA_ALIGN(na->nla_len);
	}
	if (sendto(fd, &req, req.n.nlmsg_len, 0, (struct sockaddr *)&addr,
		   sizeof(addr)) < 0)
		return -1;
	r = recv(fd, buf, sizeof(buf), 0);
	if (r <= 0)
		return -1;

	for (nlh = (struct nlmsghdr *)buf; NLMSG_OK(nlh, (unsigned int)r);
	     nlh = NLMSG_NEXT(nlh, r)) {
		struct genlmsghdr *gh = NLMSG_DATA(nlh);
		int len = nlh->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
		struct nlattr *na = (struct nlattr *)((char *)gh + GENL_HDRLEN);

		for (; NLA_OK(na, len); na = NLA_NEXT(na, len)) {
			int glen = na->nla_len - NLA_HDRLEN;
			struct nlattr *g = (struct nlattr *)((char *)na +
							     NLA_HDRLEN);

			if (na->nla_type != CTRL_ATTR_MCAST_GROUPS)
				continue;
			for (; NLA_OK(g, glen); g = NLA_NEXT(g, glen)) {
				int ilen = g->nla_len - NLA_HDRLEN;
				struct nlattr *i = (struct nlattr *)((char *)g +
							     NLA_HDRLEN);
				const char *name = NULL;
				unsigned int id = 0;

				for (; NLA_OK(i, ilen); i = NLA_NEXT(i, ilen)) {
					if (i->nla_type == CTRL_ATTR_MCAST_GRP_NAME)
						name = (char *)i + NLA_HDRLEN;
					else if (i->nla_type == CTRL_ATTR_MCAST_GRP_ID)
						id = *(unsigned int *)((char *)i +
								       NLA_HDRLEN);
				}
				if (name && !strcmp(name, group))
					gid = id;
			}
		}
	}
	if (gid < 0)
		return -1;

	return setsockopt(fd, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &gid,
			  sizeof(gid)) < 0 ? -1 : 0;
}

struct msg {
	struct nlmsghdr n;
	struct genlmsghdr g;
	char attrs[128];
};

static struct nlattr *msg_put(struct msg *m, int type, const void *data,
			      int len)
{
	struct nlattr *na = (struct nlattr *)((char *)m +
					     NLMSG_ALIGN(m->n.nlmsg_len));

	na->nla_type = type;
	na->nla_len = NLA_HDRLEN + len;
	memcpy((char *)na + NLA_HDRLEN, data, len);
	m->n.nlmsg_len = NLMSG_ALIGN(m->n.nlmsg_len) + NLA_ALIGN(na->nla_len);
	return na;
}

static int nfc_req(int fd, int family, unsigned char cmd,
		   unsigned int dev_idx, int with_idx, unsigned int flags,
		   int print_ack)
{
	struct msg m;
	struct sockaddr_nl addr = { .nl_family = AF_NETLINK };
	struct nlmsghdr *nlh;
	char buf[4096];
	ssize_t r;
	int err = 0;

	memset(&m, 0, sizeof(m));
	m.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	m.n.nlmsg_type = family;
	m.n.nlmsg_flags = NLM_F_REQUEST | flags;
	m.n.nlmsg_seq = 2;
	m.g.cmd = cmd;
	m.g.version = 1;
	if (with_idx)
		msg_put(&m, NFC_ATTR_DEVICE_INDEX, &dev_idx,
			sizeof(dev_idx));

	if (sendto(fd, &m, m.n.nlmsg_len, 0, (struct sockaddr *)&addr,
		   sizeof(addr)) < 0)
		return -errno;
	if (!print_ack)
		return 0;

	r = recv(fd, buf, sizeof(buf), 0);
	if (r < 0)
		return -errno;
	for (nlh = (struct nlmsghdr *)buf; NLMSG_OK(nlh, (unsigned int)r);
	     nlh = NLMSG_NEXT(nlh, r)) {
		if (nlh->nlmsg_type == NLMSG_ERROR) {
			struct nlmsgerr *e = NLMSG_DATA(nlh);

			err = e->error;
		}
	}
	return err;
}

static const char *proto_name(unsigned int mask)
{
	static char name[64];

	name[0] = '\0';
	if (mask & NFC_PROTO_JEWEL_MASK)
		strcat(name, "JEWEL ");
	if (mask & NFC_PROTO_MIFARE_MASK)
		strcat(name, "MIFARE ");
	if (mask & NFC_PROTO_ISO14443_MASK)
		strcat(name, "ISO14443-A ");
	if (mask & NFC_PROTO_ISO14443_B_MASK)
		strcat(name, "ISO14443-B ");
	if (mask & NFC_PROTO_FELICA_MASK)
		strcat(name, "FELICA ");
	if (mask & NFC_PROTO_ISO15693_MASK)
		strcat(name, "ISO15693 ");
	return name[0] ? name : "(none)";
}

static void hex(const unsigned char *p, int n)
{
	for (int i = 0; i < n; i++)
		printf("%02x", p[i]);
}

static unsigned int dev_index = 0;

/* NFC_CMD_GET_TARGET is a dump: the kernel answers with one message per
 * target, each carrying NFC_ATTR_TARGET_INDEX / PROTOCOLS / NFCID1 / SEL_RES. */
static void dump_targets(unsigned int idx)
{
	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
		char attrs[64];
	} req;
	struct sockaddr_nl addr = { .nl_family = AF_NETLINK };
	char buf[8192];
	ssize_t r;
	int fd = nl_open();

	if (fd < 0)
		return;

	memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	req.n.nlmsg_type = genl_family_id(fd);
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	req.n.nlmsg_seq = 7;
	req.g.cmd = NFC_CMD_GET_TARGET;
	req.g.version = 1;
	{
		struct nlattr *na = (struct nlattr *)((char *)&req +
						NLMSG_ALIGN(req.n.nlmsg_len));

		na->nla_type = NFC_ATTR_DEVICE_INDEX;
		na->nla_len = NLA_HDRLEN + sizeof(idx);
		memcpy((char *)na + NLA_HDRLEN, &idx, sizeof(idx));
		req.n.nlmsg_len = NLMSG_ALIGN(req.n.nlmsg_len) +
				  NLA_ALIGN(na->nla_len);
	}
	if (sendto(fd, &req, req.n.nlmsg_len, 0, (struct sockaddr *)&addr,
		   sizeof(addr)) < 0) {
		close(fd);
		return;
	}

	while ((r = recv(fd, buf, sizeof(buf), 0)) > 0) {
		struct nlmsghdr *nlh;

		for (nlh = (struct nlmsghdr *)buf; NLMSG_OK(nlh, (unsigned int)r);
		     nlh = NLMSG_NEXT(nlh, r)) {
			struct genlmsghdr *gh;
			int len;
			struct nlattr *na;
			unsigned int t_idx = 0, protos = 0;
			unsigned char nfcid1[10];
			int nfcid1_len = 0, sel_res = -1;
			unsigned short sens_res = 0;

			if (nlh->nlmsg_type == NLMSG_DONE) {
				close(fd);
				return;
			}
			if (nlh->nlmsg_type == NLMSG_ERROR)
				continue;

			gh = NLMSG_DATA(nlh);
			len = nlh->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
			na = (struct nlattr *)((char *)gh + GENL_HDRLEN);
			for (; NLA_OK(na, len); na = NLA_NEXT(na, len)) {
				void *val = (char *)na + NLA_HDRLEN;

				switch (na->nla_type) {
				case NFC_ATTR_TARGET_INDEX:
					t_idx = *(unsigned int *)val;
					break;
				case NFC_ATTR_PROTOCOLS:
					protos = *(unsigned int *)val;
					break;
				case NFC_ATTR_TARGET_NFCID1:
					nfcid1_len = na->nla_len - NLA_HDRLEN;
					memcpy(nfcid1, val,
					       nfcid1_len > 10 ? 10 : nfcid1_len);
					break;
				case NFC_ATTR_TARGET_SENS_RES:
					sens_res = *(unsigned short *)val;
					break;
				case NFC_ATTR_TARGET_SEL_RES:
					sel_res = *(unsigned char *)val;
					break;
				}
			}
			printf("  target %u: proto=%s", t_idx, proto_name(protos));
			if (sens_res)
				printf(" sens_res=0x%04x", sens_res);
			if (sel_res >= 0)
				printf(" sel_res=0x%02x", sel_res);
			printf(" nfcid1=");
			hex(nfcid1, nfcid1_len);
			printf("\n");
		}
	}
	close(fd);
}

static void dump_event(struct nlmsghdr *nlh)
{
	struct genlmsghdr *gh = NLMSG_DATA(nlh);
	int len = nlh->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
	struct nlattr *na = (struct nlattr *)((char *)gh + GENL_HDRLEN);
	unsigned int target = 0, protos = 0;
	unsigned char nfcid1[10];
	int nfcid1_len = 0;

	for (; NLA_OK(na, len); na = NLA_NEXT(na, len)) {
		void *val = (char *)na + NLA_HDRLEN;

		switch (na->nla_type) {
		case NFC_ATTR_DEVICE_INDEX:
			dev_index = *(unsigned int *)val;
			break;
		case NFC_ATTR_TARGET_INDEX:
			target = *(unsigned int *)val;
			break;
		case NFC_ATTR_PROTOCOLS:
			protos = *(unsigned int *)val;
			break;
		case NFC_ATTR_TARGET_NFCID1:
			nfcid1_len = na->nla_len - NLA_HDRLEN;
			memcpy(nfcid1, val, nfcid1_len > 10 ? 10 : nfcid1_len);
			break;
		}
	}
	if (gh->cmd == NFC_EVENT_TARGETS_FOUND) {
		printf("TARGET FOUND\n");
		/* The event carries only the device index; the target itself
		 * (protocol, nfcid1, sel_res) comes from a GET_TARGET dump. */
		dump_targets(dev_index);
	} else {
		printf("event cmd=%u\n", gh->cmd);
	}
	fflush(stdout);
}

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "up";
	unsigned int dev_idx = ~0U;
	unsigned int im = NFC_PROTO_JEWEL_MASK | NFC_PROTO_MIFARE_MASK |
			  NFC_PROTO_ISO14443_MASK | NFC_PROTO_ISO14443_B_MASK |
			  NFC_PROTO_FELICA_MASK | NFC_PROTO_ISO15693_MASK;

	/* Narrowing the discovery map is a cheap way to tell "the chip does
	 * not do RF at all" from "the chip ignores part of the protocol set":
	 *   poll iso     - ISO14443-A only (MIFARE/Troika class cards)
	 *   poll mifare  - MIFARE only
	 */
	if (argc > 2 && !strcmp(argv[2], "iso"))
		im = NFC_PROTO_ISO14443_MASK;
	else if (argc > 2 && !strcmp(argv[2], "mifare"))
		im = NFC_PROTO_MIFARE_MASK;
	int fd, fam, r;

	fd = nl_open();
	if (fd < 0) {
		perror("netlink");
		return 1;
	}
	/* pick the first adapter in /sys/class/nfc — the index is not
	 * stable across module reloads */
	{
		DIR *d = opendir("/sys/class/nfc");
		struct dirent *de;

		if (d) {
			while ((de = readdir(d))) {
				unsigned int n;
				if (sscanf(de->d_name, "nfc%u", &n) == 1) {
					dev_idx = n;
					break;
				}
			}
			closedir(d);
		}
		if (dev_idx == ~0U) {
			printf("no nfc adapter in sysfs\n");
			return 1;
		}
		printf("using nfc%u\n", dev_idx);
	}
	fam = genl_family_id(fd);
	if (fam < 0) {
		printf("nfc genl family not found\n");
		return 1;
	}
	/* Target notifications are multicast; without joining the group the
	 * socket never sees NFC_EVENT_TARGETS_FOUND. */
	if (genl_join_mcast(fd, NFC_GENL_NAME, "events") < 0)
		printf("warning: could not join the nfc event group\n");

	if (!strcmp(mode, "down")) {
		r = nfc_req(fd, fam, NFC_CMD_DEV_DOWN, dev_idx, 1, NLM_F_ACK, 1);
		printf("dev_down: %s\n", r ? strerror(-r) : "OK");
		return r ? 1 : 0;
	}

	printf("NFC_CMD_DEV_UP (CORE_RESET over I2C)...\n");
	r = nfc_req(fd, fam, NFC_CMD_DEV_UP, dev_idx, 1, NLM_F_ACK, 1);
	printf("dev_up: %s\n", r ? strerror(-r) : "OK — chip answered NCI");
	/* Polling right after a manual `up` is the normal case: the device is
	 * already open, and a second DEV_UP answers EBUSY.  That is not an
	 * error for `poll`, so keep going instead of bailing out. */
	if (r && !((r == -EBUSY || r == -EALREADY) && strcmp(mode, "up"))) {
		printf("dev_up failed\n");
		return 1;
	}

	if (!strcmp(mode, "up"))
		return 0;

	/* poll */
	{
		struct msg m;
		struct sockaddr_nl addr = { .nl_family = AF_NETLINK };
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		int i;

		memset(&m, 0, sizeof(m));
		m.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
		m.n.nlmsg_type = fam;
		/* Ask for an ACK: without it a rejected START_POLL (for
		 * example an unsupported protocol in the mask, or a chip that
		 * refuses RF_DISCOVER) is indistinguishable from a silent
		 * poll that simply never finds a target. */
		m.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
		m.n.nlmsg_seq = 3;
		m.g.cmd = NFC_CMD_START_POLL;
		m.g.version = 1;
		msg_put(&m, NFC_ATTR_DEVICE_INDEX, &dev_idx, sizeof(dev_idx));
		msg_put(&m, NFC_ATTR_IM_PROTOCOLS, &im, sizeof(im));
		{
			/* NFC_ATTR_RF_MODE is NLA_U8 in the kernel policy
			 * (net/nfc/netlink.c): four bytes are rejected and the
			 * whole START_POLL message is dropped with
			 * "attribute type 11 has an invalid length". */
			unsigned char rf_mode = NFC_RF_INITIATOR;

			msg_put(&m, NFC_ATTR_RF_MODE, &rf_mode,
				sizeof(rf_mode));
		}
		if (sendto(fd, &m, m.n.nlmsg_len, 0, (struct sockaddr *)&addr,
			   sizeof(addr)) < 0) {
			perror("START_POLL");
			return 1;
		}
		{
			char abuf[512];
			struct nlmsghdr *nlh;
			ssize_t n;

			if (poll(&pfd, 1, 1000) > 0 &&
			    (n = recv(fd, abuf, sizeof(abuf), 0)) > 0) {
				nlh = (struct nlmsghdr *)abuf;
				if (nlh->nlmsg_type == NLMSG_ERROR) {
					struct nlmsgerr *e =
						(struct nlmsgerr *)NLMSG_DATA(nlh);

					if (e->error)
						printf("START_POLL rejected: %s (%d)\n",
						       strerror(-e->error), e->error);
					else
						printf("START_POLL accepted by the kernel\n");
				}
			}
		}
		printf("polling (tap a card)...\n");
		fflush(stdout);
		/* 600 * 500 ms = 5 minutes: long enough to place a card after
		 * the console has been attached, without flooding the log. */
		for (i = 0; i < 600; i++) {
			char buf[4096];
			ssize_t n;
			struct nlmsghdr *nlh;

			if (!(i % 20))
				printf("."), fflush(stdout);
			if (poll(&pfd, 1, 500) <= 0)
				continue;
			n = recv(fd, buf, sizeof(buf), 0);
			if (n <= 0)
				continue;
			for (nlh = (struct nlmsghdr *)buf;
			     NLMSG_OK(nlh, (unsigned int)n);
			     nlh = NLMSG_NEXT(nlh, n)) {
				if (nlh->nlmsg_type == NLMSG_ERROR)
					continue;
				dump_event(nlh);
			}
		}
		printf("\npoll timeout, no target\n");
	}
	return 0;
}
