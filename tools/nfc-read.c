// SPDX-License-Identifier: GPL-2.0
/*
 * nfc-read — read an NFC Forum Type 2 tag (MIFARE Ultralight / NTAG) through
 * the kernel NCI stack and print its NDEF payload.
 *
 *   nfc-read            poll, wait for a Type 2 tag, dump NDEF
 *   nfc-read --raw      also print raw pages
 *
 * Flow: NFC_CMD_DEV_UP (VEN on, CORE_RESET/INIT) -> NFC_CMD_START_POLL ->
 * NFC_EVENT_TARGETS_FOUND -> AF_NFC raw socket connect (activates the target,
 * RF_DISCOVER_SELECT) -> MIFARE READ (0x30) pages -> NDEF TLV parse.
 */
#include <errno.h>
#include <fcntl.h>
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

static void msg_put(struct msg *m, int type, const void *data, int len)
{
	struct nlattr *na = (struct nlattr *)((char *)m +
					     NLMSG_ALIGN(m->n.nlmsg_len));

	na->nla_type = type;
	na->nla_len = NLA_HDRLEN + len;
	memcpy((char *)na + NLA_HDRLEN, data, len);
	m->n.nlmsg_len = NLMSG_ALIGN(m->n.nlmsg_len) + NLA_ALIGN(na->nla_len);
}

static int nfc_simple(int fd, int family, unsigned char cmd,
		      unsigned int dev_idx)
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
	m.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	m.n.nlmsg_seq = 2;
	m.g.cmd = cmd;
	m.g.version = 1;
	msg_put(&m, NFC_ATTR_DEVICE_INDEX, &dev_idx, sizeof(dev_idx));
	if (sendto(fd, &m, m.n.nlmsg_len, 0, (struct sockaddr *)&addr,
		   sizeof(addr)) < 0)
		return -errno;
	r = recv(fd, buf, sizeof(buf), 0);
	if (r < 0)
		return -errno;
	for (nlh = (struct nlmsghdr *)buf; NLMSG_OK(nlh, (unsigned int)r);
	     nlh = NLMSG_NEXT(nlh, r))
		if (nlh->nlmsg_type == NLMSG_ERROR)
			err = ((struct nlmsgerr *)NLMSG_DATA(nlh))->error;
	return err;
}

static void unblock_rfkill(void)
{
	int fd = open("/sys/class/rfkill/rfkill0/soft", O_WRONLY);

	if (fd < 0)
		return;
	(void)!write(fd, "0", 1);
	close(fd);
}

/* One MIFARE READ (0x30) of 4 pages, returns 16 bytes. */
static int t2t_read(int s, unsigned char page, unsigned char *out)
{
	unsigned char cmd[2] = { 0x30, page };
	struct pollfd pfd = { .fd = s, .events = POLLIN };
	int r;

	if (write(s, cmd, sizeof(cmd)) != sizeof(cmd))
		return -1;
	if (poll(&pfd, 1, 1000) <= 0)
		return -1;
	r = read(s, out, 16);
	return r == 16 ? 0 : -1;
}

static int ndef_parse(const unsigned char *data, int len)
{
	int i = 0;

	/* Skip leading NULL TLVs, find NDEF (0x03). */
	while (i < len && data[i] == 0x00)
		i++;
	if (i >= len || data[i] != 0x03)
		return -1;
	i++;
	{
		int mlen = data[i++];

		if (mlen == 0xff) {
			mlen = (data[i] << 8) | data[i + 1];
			i += 2;
		}
		if (i + mlen > len)
			return -1;
		/* NDEF message: first byte flags, then type length, payload
		 * length, type, id, payload. */
		{
			int sr = data[i] & 0x10;
			int il = data[i] & 0x08;
			int tl = data[i + 1];
			int pl = sr ? data[i + 2] : (data[i + 2] << 24) |
						    (data[i + 3] << 16) |
						    (data[i + 4] << 8) |
						    data[i + 5];
			int off = i + 2 + (sr ? 1 : 4) + il;

			printf("NDEF: type '%.*s' len %d\n", tl,
			       (const char *)data + off, pl);
			off += tl;
			if (il) {
				off += 1 + data[off - 1];
			}
			if (tl == 1 && data[off - tl] == 'T') {
				int status = data[off];
				int lang = status & 0x3f;

				printf("text: %.*s%s\n", lang,
				       (const char *)data + off + 1,
				       (const char *)data + off + 1 + lang);
			} else if (tl == 1 && data[off - tl] == 'U') {
				printf("uri: %.70s\n",
				       (const char *)data + off + 1);
			} else {
				printf("payload:");
				for (int k = 0; k < pl && k < 64; k++)
					printf(" %02x", data[off + k]);
				printf("\n");
			}
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	int raw = argc > 1 && !strcmp(argv[1], "--raw");
	unsigned int dev_idx = 0;
	unsigned char pages[256];
	int fd, fam, s, got = 0, i;

	unblock_rfkill();
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
	if (nfc_simple(fd, fam, NFC_CMD_DEV_UP, dev_idx)) {
		printf("dev_up failed\n");
		return 1;
	}

	{
		struct msg m;
		struct sockaddr_nl addr = { .nl_family = AF_NETLINK };
		unsigned int im = NFC_PROTO_MIFARE_MASK |
				  NFC_PROTO_ISO14443_MASK;
		/* NLA_U8 in the kernel policy, see net/nfc/netlink.c */
		unsigned char rf_mode = NFC_RF_INITIATOR;

		memset(&m, 0, sizeof(m));
		m.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
		m.n.nlmsg_type = fam;
		m.n.nlmsg_flags = NLM_F_REQUEST;
		m.n.nlmsg_seq = 3;
		m.g.cmd = NFC_CMD_START_POLL;
		m.g.version = 1;
		msg_put(&m, NFC_ATTR_DEVICE_INDEX, &dev_idx, sizeof(dev_idx));
		msg_put(&m, NFC_ATTR_IM_PROTOCOLS, &im, sizeof(im));
		msg_put(&m, NFC_ATTR_RF_MODE, &rf_mode, sizeof(rf_mode));
		if (sendto(fd, &m, m.n.nlmsg_len, 0, (struct sockaddr *)&addr,
			   sizeof(addr)) < 0) {
			perror("START_POLL");
			return 1;
		}
	}
	printf("hold a Type 2 tag (NTAG/MIFARE UL) to the back of the phone...\n");

	for (i = 0; i < 120 && !got; i++) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		char buf[4096];
		ssize_t n;
		struct nlmsghdr *nlh;

		if (poll(&pfd, 1, 500) <= 0)
			continue;
		n = recv(fd, buf, sizeof(buf), 0);
		if (n <= 0)
			continue;
		for (nlh = (struct nlmsghdr *)buf; NLMSG_OK(nlh, (unsigned int)n);
		     nlh = NLMSG_NEXT(nlh, n)) {
			struct genlmsghdr *gh;
			int len;
			struct nlattr *na;
			unsigned int idx = 0, protos = 0;

			if (nlh->nlmsg_type == NLMSG_ERROR)
				continue;
			gh = NLMSG_DATA(nlh);
			if (gh->cmd != NFC_EVENT_TARGETS_FOUND)
				continue;
			len = nlh->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
			na = (struct nlattr *)((char *)gh + GENL_HDRLEN);
			for (; NLA_OK(na, len); na = NLA_NEXT(na, len)) {
				void *v = (char *)na + NLA_HDRLEN;

				if (na->nla_type == NFC_ATTR_TARGET_INDEX)
					idx = *(unsigned int *)v;
				if (na->nla_type == NFC_ATTR_PROTOCOLS)
					protos = *(unsigned int *)v;
			}
			printf("target idx=%u protocols=0x%x\n", idx, protos);

			s = socket(AF_NFC, SOCK_RAW, NFC_SOCKPROTO_RAW);
			if (s < 0) {
				perror("AF_NFC");
				return 1;
			}
			{
				struct sockaddr_nfc sa = {
					.sa_family = AF_NFC,
					.dev_idx = dev_idx,
					.target_idx = idx,
					.nfc_protocol = (protos &
							 NFC_PROTO_MIFARE_MASK) ?
							NFC_PROTO_MIFARE :
							NFC_PROTO_ISO14443,
				};

				if (connect(s, (struct sockaddr *)&sa,
					    sizeof(sa)) < 0) {
					printf("activate: %s\n",
					       strerror(errno));
					close(s);
					continue;
				}
			}
			memset(pages, 0, sizeof(pages));
			for (int p = 0; p < 16; p++) {
				if (t2t_read(s, p * 4, pages + p * 16)) {
					printf("read page %d failed\n", p * 4);
					break;
				}
			}
			if (raw) {
				for (int p = 0; p < 16; p++) {
					printf("p%02d:", p * 4);
					for (int k = 0; k < 16; k++)
						printf(" %02x", pages[p * 16 + k]);
					printf("\n");
				}
			}
			ndef_parse(pages, sizeof(pages));
			close(s);
			got = 1;
		}
	}
	if (!got)
		printf("no tag found\n");
	nfc_simple(fd, fam, NFC_CMD_DEV_DOWN, dev_idx);
	close(fd);
	return got ? 0 : 1;
}
