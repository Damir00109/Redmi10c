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
		printf("TARGET FOUND idx=%u proto=%s nfcid1=", target,
		       proto_name(protos));
		hex(nfcid1, nfcid1_len);
		printf("\n");
	} else {
		printf("event cmd=%u\n", gh->cmd);
	}
	fflush(stdout);
}

int main(int argc, char **argv)
{
	const char *mode = argc > 1 ? argv[1] : "up";
	unsigned int dev_idx = 0;
	unsigned int im = NFC_PROTO_JEWEL_MASK | NFC_PROTO_MIFARE_MASK |
			  NFC_PROTO_ISO14443_MASK | NFC_PROTO_ISO14443_B_MASK |
			  NFC_PROTO_FELICA_MASK | NFC_PROTO_ISO15693_MASK;
	int fd, fam, r;

	fd = nl_open();
	if (fd < 0) {
		perror("netlink");
		return 1;
	}
	fam = genl_family_id(fd);
	if (fam < 0) {
		printf("nfc genl family not found\n");
		return 1;
	}

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
		m.n.nlmsg_flags = NLM_F_REQUEST;
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
