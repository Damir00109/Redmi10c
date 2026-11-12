// SPDX-License-Identifier: GPL-2.0
/*
 * nfc-mfread — MIFARE Classic block read probe over the kernel NCI stack.
 *
 * Flow mirrors nfc-nci-test (proven on-device): DEV_UP, START_POLL with
 * NLM_F_ACK + NFC_ATTR_RF_MODE as u8, wait for the TARGETS_FOUND multicast
 * event, then GET_TARGET dump on a dedicated socket.  Afterwards a raw
 * AF_NFC SOCK_SEQPACKET socket does AUTH_KEY_A + READ on sector 0.
 *
 *   nfc-mfread            try MAD/NFC/default keys, dump block 0
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
#include <sys/time.h>
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

/* NFC_EVENT_* are multicast on the "events" group — join it or the socket
 * only sees ACKs (same helper as in nfc-nci-test.c). */
static int genl_join_mcast(int fd, const char *family, const char *group)
{
	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
		char attrs[256];
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

static struct nlattr *msg_put(struct msg *m, int type,
			      const void *data, int len)
{
	struct nlattr *na = (struct nlattr *)((char *)m +
					     NLMSG_ALIGN(m->n.nlmsg_len));

	na->nla_type = type;
	na->nla_len = NLA_HDRLEN + len;
	memcpy((char *)na + NLA_HDRLEN, data, len);
	m->n.nlmsg_len = NLMSG_ALIGN(m->n.nlmsg_len) + NLA_ALIGN(na->nla_len);
	return na;
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
	int i, fd;

	for (i = 0; i < 8; i++) {
		char p[64];
		ssize_t n;
		char name[32] = {0};

		snprintf(p, sizeof(p), "/sys/class/rfkill/rfkill%d/name", i);
		fd = open(p, O_RDONLY);
		if (fd < 0)
			continue;
		n = read(fd, name, sizeof(name) - 1);
		close(fd);
		if (n <= 0)
			continue;
		if (strstr(name, "nfc")) {
			snprintf(p, sizeof(p),
				 "/sys/class/rfkill/rfkill%d/soft", i);
			fd = open(p, O_WRONLY);
			if (fd >= 0) {
				(void)!write(fd, "0", 1);
				close(fd);
			}
		}
	}
}

/* send() + recv() with errno reporting */
static int xchg(int s, const unsigned char *tx, int txlen,
		unsigned char *rx, int rxlen, int timeout_ms)
{
	struct pollfd pfd = { .fd = s, .events = POLLIN };
	ssize_t r;
	int pr;

	errno = 0;
	r = send(s, tx, txlen, 0);
	if (r < 0) {
		printf("    send(%d): errno=%d (%s)\n", txlen, errno,
		       strerror(errno));
		return -errno;
	}
	if (r != txlen) {
		printf("    send short: %zd/%d\n", r, txlen);
		return -EIO;
	}
	pr = poll(&pfd, 1, timeout_ms);
	if (pr == 0) {
		printf("    recv: timeout\n");
		return -ETIMEDOUT;
	}
	if (pr < 0) {
		printf("    poll: errno=%d (%s)\n", errno, strerror(errno));
		return -errno;
	}
	errno = 0;
	r = recv(s, rx, rxlen, 0);
	if (r < 0) {
		printf("    recv: errno=%d (%s)\n", errno, strerror(errno));
		return -errno;
	}
	printf("    recv: %zd bytes:", r);
	for (ssize_t i = 0; i < r && i < 24; i++)
		printf(" %02x", rx[i]);
	printf("\n");
	return (int)r;
}

/* NFC_CMD_GET_TARGET is a dump on a dedicated socket (pending ACKs and
 * multicast events on the poll socket confuse the parser). */
static int get_target(int family, unsigned int dev_idx,
		      unsigned int *idx_out, unsigned int *protos_out,
		      unsigned char *uid_out, int *uid_len_out)
{
	struct {
		struct nlmsghdr n;
		struct genlmsghdr g;
		char attrs[64];
	} req;
	struct sockaddr_nl addr = { .nl_family = AF_NETLINK };
	char buf[8192];
	ssize_t r;
	int found = 0;
	int dfd = nl_open();
	struct timeval tv = { .tv_sec = 1 };

	if (dfd < 0)
		return 0;
	setsockopt(dfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	req.n.nlmsg_type = family;
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
	req.n.nlmsg_seq = 8;
	req.g.cmd = NFC_CMD_GET_TARGET;
	req.g.version = 1;
	{
		struct nlattr *na = (struct nlattr *)((char *)&req +
						NLMSG_ALIGN(req.n.nlmsg_len));

		na->nla_type = NFC_ATTR_DEVICE_INDEX;
		na->nla_len = NLA_HDRLEN + sizeof(dev_idx);
		memcpy((char *)na + NLA_HDRLEN, &dev_idx, sizeof(dev_idx));
		req.n.nlmsg_len = NLMSG_ALIGN(req.n.nlmsg_len) +
				  NLA_ALIGN(na->nla_len);
	}
	if (sendto(dfd, &req, req.n.nlmsg_len, 0, (struct sockaddr *)&addr,
		   sizeof(addr)) < 0) {
		close(dfd);
		return 0;
	}

	while ((r = recv(dfd, buf, sizeof(buf), 0)) > 0) {
		struct nlmsghdr *nlh;

		for (nlh = (struct nlmsghdr *)buf; NLMSG_OK(nlh, (unsigned int)r);
		     nlh = NLMSG_NEXT(nlh, r)) {
			struct genlmsghdr *gh;
			int len;
			struct nlattr *na;
			unsigned int idx = 0, protos = 0;
			unsigned char uid[10] = {0};
			int uid_len = 0;

			if (nlh->nlmsg_type == NLMSG_DONE ||
			    nlh->nlmsg_type == NLMSG_ERROR) {
				close(dfd);
				return found;
			}
			gh = NLMSG_DATA(nlh);
			len = nlh->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
			na = (struct nlattr *)((char *)gh + GENL_HDRLEN);
			for (; NLA_OK(na, len); na = NLA_NEXT(na, len)) {
				void *v = (char *)na + NLA_HDRLEN;

				if (na->nla_type == NFC_ATTR_TARGET_INDEX)
					idx = *(unsigned int *)v;
				if (na->nla_type == NFC_ATTR_PROTOCOLS)
					protos = *(unsigned int *)v;
				if (na->nla_type == NFC_ATTR_TARGET_NFCID1) {
					uid_len = na->nla_len - NLA_HDRLEN;
					memcpy(uid, v, uid_len > 10 ? 10 : uid_len);
				}
			}
			if (idx || protos || uid_len) {
				*idx_out = idx;
				*protos_out = protos;
				memcpy(uid_out, uid, uid_len);
				*uid_len_out = uid_len;
				found = 1;
			}
		}
	}
	close(dfd);
	return found;
}

int main(int argc, char **argv)
{
	static const unsigned char key_mad[6] =
		{ 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5 };
	static const unsigned char key_nfc[6] =
		{ 0xd3, 0xf7, 0xd3, 0xf7, 0xd3, 0xf7 };
	static const unsigned char key_def[6] =
		{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	unsigned int dev_idx = 0;
	int fd, fam, s, i, next_key = 0;

	int t2mode = argc > 1 && !strcmp(argv[1], "t2");

	(void)argc;
	setvbuf(stdout, NULL, _IONBF, 0);
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
	if (genl_join_mcast(fd, NFC_GENL_NAME, "events") < 0)
		printf("warning: could not join the nfc event group\n");
	{
		int r;

		r = nfc_simple(fd, fam, NFC_CMD_DEV_DOWN, dev_idx);
		printf("dev_down: %d\n", r);
		r = nfc_simple(fd, fam, NFC_CMD_DEV_UP, dev_idx);
		printf("dev_up: %d\n", r);
		if (r) {
			printf("dev_up failed\n");
			return 1;
		}
	}

	{
		struct msg m;
		struct sockaddr_nl addr = { .nl_family = AF_NETLINK };
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		unsigned int im = NFC_PROTO_JEWEL_MASK |
				  NFC_PROTO_MIFARE_MASK |
				  NFC_PROTO_FELICA_MASK |
				  NFC_PROTO_ISO14443_MASK |
				  NFC_PROTO_ISO14443_B_MASK |
				  NFC_PROTO_ISO15693_MASK;
		unsigned char rf_mode = NFC_RF_INITIATOR;

		memset(&m, 0, sizeof(m));
		m.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
		m.n.nlmsg_type = fam;
		m.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
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
		{
			char abuf[512];
			struct nlmsghdr *nlh;
			ssize_t n;

			if (poll(&pfd, 1, 1000) > 0 &&
			    (n = recv(fd, abuf, sizeof(abuf), 0)) > 0) {
				for (nlh = (struct nlmsghdr *)abuf;
				     NLMSG_OK(nlh, (unsigned int)n);
				     nlh = NLMSG_NEXT(nlh, n)) {
					if (nlh->nlmsg_type == NLMSG_ERROR) {
						struct nlmsgerr *e =
							(struct nlmsgerr *)
							NLMSG_DATA(nlh);
						if (e->error)
							printf("START_POLL rejected: %s (%d)\n",
							       strerror(-e->error),
							       e->error);
					}
				}
			}
		}
	}
	printf("hold the MIFARE card to the phone...\n");

	for (i = 0; i < 120; i++) {
		struct pollfd pfd = { .fd = fd, .events = POLLIN };
		char buf[4096];
		ssize_t n;
		unsigned int idx = 0, protos = 0;
		unsigned char uid[10] = {0};
		int uid_len = 0;
		int have_target = 0;

		/* Drain events; remember the device index they carry. */
		while (poll(&pfd, 1, 0) > 0 &&
		       (n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT)) > 0) {
			struct nlmsghdr *nlh;

			for (nlh = (struct nlmsghdr *)buf;
			     NLMSG_OK(nlh, (unsigned int)n);
			     nlh = NLMSG_NEXT(nlh, n)) {
				struct genlmsghdr *gh;
				int elen;
				struct nlattr *ea;

				if (nlh->nlmsg_type == NLMSG_ERROR)
					continue;
				gh = NLMSG_DATA(nlh);
				if (gh->cmd != NFC_EVENT_TARGETS_FOUND)
					continue;
				elen = nlh->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
				ea = (struct nlattr *)((char *)gh + GENL_HDRLEN);
				for (; NLA_OK(ea, elen); ea = NLA_NEXT(ea, elen))
					if (ea->nla_type == NFC_ATTR_DEVICE_INDEX)
						dev_idx = *(unsigned int *)
							((char *)ea + NLA_HDRLEN);
				have_target = 1;
			}
		}

		if (!have_target &&
		    !get_target(fam, dev_idx, &idx, &protos, uid, &uid_len)) {
			usleep(500 * 1000);
			continue;
		}
		if (have_target &&
		    !get_target(fam, dev_idx, &idx, &protos, uid, &uid_len)) {
			usleep(200 * 1000);
			continue;
		}
		printf("target idx=%u protos=0x%x uid=", idx, protos);
		for (int j = 0; j < uid_len; j++)
			printf("%02x", uid[j]);
		printf("\n");

		{
			const unsigned char *keys[3] =
				{ key_mad, key_nfc, key_def };
			const char *names[3] =
				{ "MAD a0..a5", "NFC d3f7..", "default ff.." };
			unsigned char tx[16], rx[64];
			int k, done = 0;

			if (t2mode) {
				struct sockaddr_nfc sa = {
					.sa_family = AF_NFC,
					.dev_idx = dev_idx,
					.target_idx = idx,
					.nfc_protocol = NFC_PROTO_MIFARE,
				};

				s = socket(AF_NFC, SOCK_SEQPACKET,
					   NFC_SOCKPROTO_RAW);
				if (s < 0 || connect(s, (struct sockaddr *)&sa,
						     sizeof(sa)) < 0) {
					printf("connect: errno=%d (%s)\n",
					       errno, strerror(errno));
					close(s);
					usleep(500 * 1000);
					continue;
				}
				printf("connected, protos=0x%x\n", protos);
				/* Type 2 / Ultralight-NTAG: dump pages via
				 * READ (0x30 + 16-bit byte address) */
				unsigned char tx[3], rx[64];
				for (int pg = 0; pg < 0x30; pg += 4) {
					tx[0] = 0x30;
					tx[1] = pg >> 8;
					tx[2] = pg & 0xff;
					int r = xchg(s, tx, 3, rx,
						     sizeof(rx), 3000);
					if (r <= 0) {
						printf("read @%02x: %s\n", pg,
						       r < 0 ? "timeout" : "err");
						break;
					}
					printf("pg %02x-%02x:", pg, pg + r / 4 - 1);
					for (int b = 0; b < r; b++)
						printf(" %02x", rx[b]);
					printf("\n");
					if (r < 16)
						break;
				}
				close(s);
				return 0;
			}

			for (k = next_key; k < 3 && !done; k++) {
				int txlen, r;

				printf("key %s:\n", names[k]);
				s = socket(AF_NFC, SOCK_SEQPACKET,
					   NFC_SOCKPROTO_RAW);
				if (s < 0) {
					perror("AF_NFC");
					return 1;
				}
				{
					struct sockaddr_nfc sa = {
						.sa_family = AF_NFC,
						.dev_idx = dev_idx,
						.target_idx = idx,
						.nfc_protocol = NFC_PROTO_MIFARE,
					};

					if (connect(s, (struct sockaddr *)&sa,
						    sizeof(sa)) < 0) {
						printf("connect: errno=%d (%s)\n",
						       errno, strerror(errno));
						close(s);
						break; /* stale target: re-poll */
					}
				}
				printf("connected, protos=0x%x\n", protos);

				txlen = 0;
				tx[txlen++] = 0x60; /* AUTH_KEY_A */
				tx[txlen++] = 0x00; /* block 0 */
				memcpy(tx + txlen, keys[k], 6);
				txlen += 6;
				/* MIFARE Classic auth uses the 4-byte NUID,
				 * even for cards with a 7-byte UID */
				int nuid_len = uid_len > 4 ? 4 : uid_len;
				memcpy(tx + txlen, uid, nuid_len);
				txlen += nuid_len;
				r = xchg(s, tx, txlen, rx, sizeof(rx), 6000);
				if (r < 0) {
					/* auth failed: link dead, next key
					 * needs a fresh activation */
					printf("    auth failed\n");
					close(s);
					next_key = k + 1;
					break;
				}
				/* auth ok: 4-byte uid + 4-byte BCC */
				tx[0] = 0x30;
				tx[1] = 0x00;
				r = xchg(s, tx, 2, rx, sizeof(rx), 6000);
				if (r >= 16) {
					printf("BLOCK0:");
					for (int b = 0; b < 16; b++)
						printf(" %02x", rx[b]);
					printf("\n");
					done = 1;
				}
				close(s);
				if (!done)
					next_key = k + 1;
			}
			if (done)
				return 0;
			if (next_key >= 3) {
				printf("all keys failed\n");
				return 1;
			}
			usleep(500 * 1000);
		}
	}
	printf("no tag found\n");
	nfc_simple(fd, fam, NFC_CMD_DEV_DOWN, dev_idx);
	close(fd);
	return 1;
}
