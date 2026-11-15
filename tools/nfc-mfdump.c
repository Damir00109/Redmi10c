// MIFARE Classic dump tool for the SN100U proprietary interface.
// genl "nfc": DEV_UP + START_POLL, wait TARGETS_FOUND, then AF_NFC
// SOCK_SEQPACKET raw socket; speaks the SN100 '40'/'10' wrappers.
//
// Usage:
//   nfc-mfdump            interactive hex TX/RX (like nfc-mfrw)
//   nfc-mfdump dump [key] dump all sectors/blocks; key = 12 hex chars
//                        (default tries a0a1a2a3a4a5, then common keys)
//   argv[1] can also be a number = seconds to wait for a card (30).
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/genetlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_link.h>
#include <arpa/inet.h>

#define NFC_SOCKPROTO_RAW 0
#define NETLINK_GENERIC 16
#define NFC_PROTO_MIFARE 2

/* nfc.h uapi enums */
enum {
	NFC_CMD_UNSPEC, NFC_CMD_GET_DEVICE, NFC_CMD_DEV_UP, NFC_CMD_DEV_DOWN,
	NFC_CMD_DEP_LINK_UP, NFC_CMD_DEP_LINK_DOWN, NFC_CMD_START_POLL,
	NFC_CMD_STOP_POLL, NFC_CMD_GET_TARGET, NFC_EVENT_TARGETS_FOUND,
	NFC_EVENT_DEVICE_ADDED, NFC_EVENT_DEVICE_REMOVED,
	NFC_EVENT_TARGET_LOST, NFC_EVENT_TM_ACTIVATED, NFC_EVENT_TM_DEACTIVATED,
	NFC_CMD_LLC_GET_PARAMS, NFC_CMD_LLC_SET_PARAMS, NFC_CMD_ENABLE_SE,
	NFC_CMD_DISABLE_SE, NFC_CMD_LLC_SDREQ, NFC_EVENT_LLC_SDRES,
	NFC_CMD_FW_DOWNLOAD, NFC_EVENT_SE_ADDED, NFC_EVENT_SE_REMOVED,
	NFC_EVENT_SE_CONNECTIVITY, NFC_EVENT_SE_TRANSACTION, NFC_CMD_GET_SE,
	NFC_CMD_SE_IO, NFC_CMD_ACTIVATE_TARGET, NFC_CMD_VENDOR,
	NFC_CMD_DEACTIVATE_TARGET,
};
enum {
	NFC_ATTR_UNSPEC, NFC_ATTR_DEVICE_INDEX, NFC_ATTR_DEVICE_NAME,
	NFC_ATTR_PROTOCOLS, NFC_ATTR_TARGET_INDEX, NFC_ATTR_TARGET_SENS_RES,
	NFC_ATTR_TARGET_SEL_RES, NFC_ATTR_TARGET_NFCID1, NFC_ATTR_TARGET_SENSB_RES,
	NFC_ATTR_TARGET_SENSF_RES, NFC_ATTR_COMM_MODE, NFC_ATTR_RF_MODE,
	NFC_ATTR_DEVICE_POWERED, NFC_ATTR_IM_PROTOCOLS, NFC_ATTR_TM_PROTOCOLS,
};

struct sockaddr_nfc {
	unsigned short sa_family;
	uint32_t dev_idx;
	uint32_t target_idx;
	uint32_t nfc_protocol;
};

static int nlfd, nfc_family, nfc_family_id_val;
static uint32_t seqno = 1;

static void addattr(struct nlmsghdr *n, int type, const void *data, int len)
{
	struct rtattr *rta = (void *)((char *)n + NLMSG_ALIGN(n->nlmsg_len));
	rta->rta_type = type;
	rta->rta_len = RTA_LENGTH(len);
	if (len) memcpy(RTA_DATA(rta), data, len);
	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(RTA_LENGTH(len));
}

static struct nlmsghdr *genlmsg(int cmd, int flags)
{
	static char buf[8192];
	memset(buf, 0, sizeof(buf));
	struct nlmsghdr *n = (void *)buf;
	n->nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	n->nlmsg_type = nfc_family;
	n->nlmsg_flags = NLM_F_REQUEST | flags;
	n->nlmsg_seq = seqno++;
	struct genlmsghdr *g = NLMSG_DATA(n);
	g->cmd = cmd;
	g->version = 1;
	return n;
}

static char pend[65536];
static int pend_len;

static void stash(const char *buf, int len)
{
	if (pend_len + len <= (int)sizeof(pend)) {
		memcpy(pend + pend_len, buf, len);
		pend_len += len;
	}
}

static int nl_talk(struct nlmsghdr *n)
{
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK };
	uint32_t want = n->nlmsg_seq;
	if (sendto(nlfd, n, n->nlmsg_len, 0, (void *)&sa, sizeof(sa)) < 0)
		return -errno;
	char buf[16384];
	for (int tries = 0; tries < 40; tries++) {
		int len = recv(nlfd, buf, sizeof(buf), 0);
		if (len < 0) return -errno;
		for (struct nlmsghdr *h = (void *)buf; NLMSG_OK(h, len); h = NLMSG_NEXT(h, len)) {
			if (h->nlmsg_seq != want) { stash((char *)h, h->nlmsg_len); continue; }
			if (h->nlmsg_type == NLMSG_ERROR) {
				struct nlmsgerr *e = NLMSG_DATA(h);
				return e->error;
			}
			return 0;
		}
	}
	return -ETIME;
}

#define CTRL_ATTR_MCAST_GROUPS 8
#define CTRL_ATTR_MCAST_GRP_NAME 1
#define CTRL_ATTR_MCAST_GRP_ID 2
static int nfc_event_group = 0;

static int genl_family_id(const char *name)
{
	int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK };
	bind(fd, (void *)&sa, sizeof(sa));
	char buf[8192] = {0};
	struct nlmsghdr *n = (void *)buf;
	n->nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	n->nlmsg_type = GENL_ID_CTRL;
	n->nlmsg_flags = NLM_F_REQUEST;
	n->nlmsg_seq = 1;
	struct genlmsghdr *g = NLMSG_DATA(n);
	g->cmd = CTRL_CMD_GETFAMILY;
	g->version = 1;
	addattr(n, CTRL_ATTR_FAMILY_NAME, name, strlen(name) + 1);
	sendto(fd, n, n->nlmsg_len, 0, (void *)&sa, sizeof(sa));
	int len = recv(fd, buf, sizeof(buf), 0);
	close(fd);
	for (struct nlmsghdr *h = (void *)buf; NLMSG_OK(h, len); h = NLMSG_NEXT(h, len)) {
		if (h->nlmsg_type == NLMSG_ERROR) return -1;
		struct genlmsghdr *g2 = NLMSG_DATA(h);
		struct rtattr *a = (void *)((char *)g2 + GENL_HDRLEN);
		int alen = h->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
		for (; RTA_OK(a, alen); a = RTA_NEXT(a, alen)) {
			if (a->rta_type == CTRL_ATTR_FAMILY_ID)
				nfc_family_id_val = *(uint16_t *)RTA_DATA(a);
			if ((a->rta_type & NLA_TYPE_MASK) == CTRL_ATTR_MCAST_GROUPS) {
				struct rtattr *g_a = RTA_DATA(a);
				int glen = RTA_PAYLOAD(a);
				for (; RTA_OK(g_a, glen); g_a = RTA_NEXT(g_a, glen)) {
					if ((g_a->rta_type & NLA_TYPE_MASK) != NLA_F_NESTED) continue;
					struct rtattr *ga = RTA_DATA(g_a);
					int galen = RTA_PAYLOAD(g_a);
					const char *gname = NULL; uint32_t gid = 0;
					for (; RTA_OK(ga, galen); ga = RTA_NEXT(ga, galen)) {
						if ((ga->rta_type & NLA_TYPE_MASK) == CTRL_ATTR_MCAST_GRP_NAME)
							gname = RTA_DATA(ga);
						if ((ga->rta_type & NLA_TYPE_MASK) == CTRL_ATTR_MCAST_GRP_ID)
							gid = *(uint32_t *)RTA_DATA(ga);
					}
					if (gname && !strcmp(gname, "events"))
						nfc_event_group = gid;
				}
			}
		}
		return nfc_family_id_val;
	}
	return -1;
}

static void dump_attrs(struct nlmsghdr *h)
{
	struct genlmsghdr *g = NLMSG_DATA(h);
	struct rtattr *a = (void *)((char *)g + GENL_HDRLEN);
	int alen = h->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
	printf("evt cmd=%u attrs:", g->cmd);
	for (; RTA_OK(a, alen); a = RTA_NEXT(a, alen)) {
		int t = a->rta_type & NLA_TYPE_MASK;
		int l = RTA_PAYLOAD(a);
		printf(" [%d]", t);
		if (t == NFC_ATTR_TARGET_INDEX || t == NFC_ATTR_PROTOCOLS ||
		    t == NFC_ATTR_IM_PROTOCOLS || t == NFC_ATTR_DEVICE_INDEX) {
			if (l >= 4) printf("=%u", *(uint32_t *)RTA_DATA(a));
		}
		if (l && l <= 24) {
			printf(" ");
			for (int i = 0; i < l; i++) printf("%02x", ((uint8_t *)RTA_DATA(a))[i]);
		}
	}
	printf("\n");
}


static int genl_dump_targets(uint32_t *tidx, uint32_t *proto, uint8_t *uid, int *uidlen)
{
	struct nlmsghdr *n = genlmsg(NFC_CMD_GET_TARGET, NLM_F_DUMP | NLM_F_ACK);
	uint32_t idx = 0;
	addattr(n, NFC_ATTR_DEVICE_INDEX, &idx, 4);
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK };
	uint32_t want = n->nlmsg_seq;
	if (sendto(nlfd, n, n->nlmsg_len, 0, (void *)&sa, sizeof(sa)) < 0)
		return -errno;
	char buf[16384];
	int found = 0;
	for (;;) {
		int len = recv(nlfd, buf, sizeof(buf), 0);
		if (len < 0) return -errno;
		for (struct nlmsghdr *h = (void *)buf; NLMSG_OK(h, len); h = NLMSG_NEXT(h, len)) {
			if (h->nlmsg_type == NLMSG_DONE) return found ? 0 : -ENODEV;
			if (h->nlmsg_type == NLMSG_ERROR) {
				struct nlmsgerr *e = NLMSG_DATA(h);
				return e->error;
			}
			struct genlmsghdr *g = NLMSG_DATA(h);
			if (g->cmd != NFC_CMD_GET_TARGET) continue;
			struct rtattr *a = (void *)((char *)g + GENL_HDRLEN);
			int alen = h->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
			uint32_t ti = 0, pr = 0;
			uint8_t u[16]; int ul = 0;
			for (; RTA_OK(a, alen); a = RTA_NEXT(a, alen)) {
				int t = a->rta_type & NLA_TYPE_MASK;
				if (t == NFC_ATTR_TARGET_INDEX && RTA_PAYLOAD(a) >= 4)
					ti = *(uint32_t *)RTA_DATA(a);
				if (t == NFC_ATTR_PROTOCOLS && RTA_PAYLOAD(a) >= 4)
					pr = *(uint32_t *)RTA_DATA(a);
				if (t == NFC_ATTR_TARGET_NFCID1) {
					ul = RTA_PAYLOAD(a);
					memcpy(u, RTA_DATA(a), ul > 16 ? 16 : ul);
				}
			}
			if (!found) {
				*tidx = ti; *proto = pr; *uidlen = ul;
				memcpy(uid, u, ul);
				found = 1;
			}
			printf("dump target idx=%u protos=0x%x uidlen=%d\n", ti, pr, ul);
		}
	}
}

static int wait_target(uint32_t *tidx, uint32_t *proto, uint8_t *uid, int *uidlen,
		       int timeout_sec)
{
	time_t deadline = timeout_sec > 0 ? time(NULL) + timeout_sec : 0;
	for (;;) {
		if (deadline && time(NULL) > deadline) {
			fprintf(stderr, "no target within %ds, shutting down\n",
				timeout_sec);
			return -ETIMEDOUT;
		}
		char buf[16384];
		int len;
		if (pend_len) {
			len = pend_len;
			if (len > (int)sizeof(buf)) len = sizeof(buf);
			memcpy(buf, pend, len);
			if (len < pend_len) memmove(pend, pend + len, pend_len - len);
			pend_len -= len;
		} else {
			len = recv(nlfd, buf, sizeof(buf), 0);
			if (len < 0) { perror("recv"); return -1; }
		}
		for (struct nlmsghdr *h = (void *)buf; NLMSG_OK(h, len); h = NLMSG_NEXT(h, len)) {
			if (h->nlmsg_type == NLMSG_ERROR) {
				struct nlmsgerr *e = NLMSG_DATA(h);
				fprintf(stderr, "nlerr %d\n", e->error);
				continue;
			}
			struct genlmsghdr *g = NLMSG_DATA(h);
			if (g->cmd != NFC_EVENT_TARGETS_FOUND) { dump_attrs(h); continue; }
			struct rtattr *a = (void *)((char *)g + GENL_HDRLEN);
			int alen = h->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
			for (; RTA_OK(a, alen); a = RTA_NEXT(a, alen)) {
				int t = a->rta_type & NLA_TYPE_MASK;
				if (t == NFC_ATTR_TARGET_INDEX && RTA_PAYLOAD(a)>=4)
					*tidx = *(uint32_t *)RTA_DATA(a);
				if (t == NFC_ATTR_PROTOCOLS && RTA_PAYLOAD(a)>=4)
					*proto = *(uint32_t *)RTA_DATA(a);
				if (t == NFC_ATTR_TARGET_NFCID1) {
					*uidlen = RTA_PAYLOAD(a);
					memcpy(uid, RTA_DATA(a), *uidlen > 16 ? 16 : *uidlen);
				}
			}
			printf("TARGETS_FOUND, dumping...\n");
			return genl_dump_targets(tidx, proto, uid, uidlen);
		}
	}
}

static void nfc_shutdown(void)
{
	struct nlmsghdr *n;
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK };

	n = genlmsg(NFC_CMD_STOP_POLL, NLM_F_ACK);
	uint32_t idx = 0;
	addattr(n, NFC_ATTR_DEVICE_INDEX, &idx, 4);
	nl_talk(n);

	n = genlmsg(NFC_CMD_DEV_DOWN, NLM_F_ACK);
	addattr(n, NFC_ATTR_DEVICE_INDEX, &idx, 4);
	nl_talk(n);
}

static void on_term(int sig)
{
	nfc_shutdown();
	_exit(0);
}

static int hex2bin(const char *s, uint8_t *out, int max)
{
	int n = 0;
	while (s[0] && s[1] && n < max) {
		unsigned v;
		if (sscanf(s, "%2x", &v) != 1) break;
		out[n++] = v;
		s += 2;
		while (*s == ' ') s++;
	}
	return n;
}

/* send payload, wait one response; returns rx len or -errno */
static int nfc_xact(int s, const uint8_t *tx, int txlen,
		    uint8_t *rx, int rxmax)
{
	if (send(s, tx, txlen, 0) < 0)
		return -errno;
	struct timeval tv = { .tv_sec = 3 };
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	return recv(s, rx, rxmax, 0);
}

static int open_target(int wait_sec, int *s_out, uint32_t *tidx_out)
{
	uint32_t tidx = 0, proto = 0; uint8_t uid[16]; int uidlen = 0;
	if (wait_target(&tidx, &proto, uid, &uidlen, wait_sec))
		return -1;
	printf("target idx=%u protos=0x%x uid=", tidx, proto);
	for (int i = 0; i < uidlen; i++) printf("%02x", uid[i]);
	printf("\n");

	struct sockaddr_nfc sn = { .sa_family = AF_NFC, .dev_idx = 0,
		.target_idx = tidx, .nfc_protocol = NFC_PROTO_MIFARE };
	int s = -1;
	for (int attempt = 0; attempt < 8 && s < 0; attempt++) {
		if (attempt) usleep(100000);
		s = socket(AF_NFC, SOCK_SEQPACKET, NFC_SOCKPROTO_RAW);
		if (s < 0) { perror("socket AF_NFC"); return -1; }
		if (connect(s, (void *)&sn, sizeof(sn)) < 0) {
			close(s); s = -1;
		}
	}
	if (s < 0) { fprintf(stderr, "connect failed\n"); return -1; }
	*s_out = s; *tidx_out = tidx;
	return 0;
}

static void hexline(const uint8_t *b, int n)
{
	for (int i = 0; i < n; i++) printf("%02x", b[i]);
}

/* one dump attempt with a single key over all 16 sectors.
 * Returns number of sectors fully read, or -1 if the link died. */
static int dump_with_key(int wait_sec, const uint8_t key[6])
{
	int s, tidx;
	if (open_target(wait_sec, &s, &tidx)) return -1;

	int sectors_ok = 0;
	uint8_t tx[16], rx[512];
	for (int sec = 0; sec < 16; sec++) {
		/* auth: 40 <sector> 10 <keyA> */
		tx[0] = 0x40; tx[1] = sec; tx[2] = 0x10;
		memcpy(tx + 3, key, 6);
		int r = nfc_xact(s, tx, 9, rx, sizeof(rx));
		if (r != 2 || rx[0] != 0x00 || rx[1] != 0x40) {
			printf("sector %2d: auth FAIL", sec);
			if (r > 0) { printf(" rx="); hexline(rx, r > 8 ? 8 : r); }
			printf("\n");
			close(s);
			return sectors_ok; /* link dead after NACK */
		}
		printf("sector %2d:", sec);
		for (int i = 0; i < 4; i++) {
			int blk = sec * 4 + i;
			tx[0] = 0x10; tx[1] = 0x30; tx[2] = blk;
			r = nfc_xact(s, tx, 3, rx, sizeof(rx));
			if (r < 18 || rx[0] != 0x00 || rx[1] != 0x10) {
				printf(" blk%d:FAIL", blk);
				close(s);
				return sectors_ok;
			}
			printf(" %02x:", blk);
			hexline(rx + 2, 16);
		}
		printf("\n");
		sectors_ok++;
	}
	close(s);
	return sectors_ok;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	nfc_family = genl_family_id("nfc");
	if (nfc_family < 0) { fprintf(stderr, "no nfc genl family\n"); return 1; }
	printf("nfc genl family=%d\n", nfc_family);

	nlfd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK };
	sa.nl_groups = 0xffffffffu;
	printf("event group=%d mask=0x%x\n", nfc_event_group, sa.nl_groups);
	bind(nlfd, (void *)&sa, sizeof(sa));

	struct nlmsghdr *n;
	n = genlmsg(NFC_CMD_DEV_DOWN, NLM_F_ACK);
	addattr(n, NFC_ATTR_DEVICE_INDEX, &(uint32_t){0}, 4);
	printf("DEV_DOWN: %d\n", nl_talk(n));
	n = genlmsg(NFC_CMD_DEV_UP, NLM_F_ACK);
	addattr(n, NFC_ATTR_DEVICE_INDEX, &(uint32_t){0}, 4);
	printf("DEV_UP: %d\n", nl_talk(n));

	n = genlmsg(NFC_CMD_START_POLL, NLM_F_ACK);
	uint32_t idx = 0, im = 0x00000004, tm = 0;
	addattr(n, NFC_ATTR_DEVICE_INDEX, &idx, 4);
	addattr(n, NFC_ATTR_IM_PROTOCOLS, &im, 4);
	addattr(n, NFC_ATTR_TM_PROTOCOLS, &tm, 4);
	printf("START_POLL: %d\n", nl_talk(n));
	atexit(nfc_shutdown);
	{
		struct sigaction sa2 = { .sa_handler = on_term };
		sigemptyset(&sa2.sa_mask);
		sigaction(SIGTERM, &sa2, NULL);
		sigaction(SIGINT, &sa2, NULL);
	}

	/* argv: [dump [key12hex] | wait_seconds] */
	int wait_sec = 30;
	int dump_mode = 0;
	uint8_t key[6] = { 0 };
	int have_key = 0;
	if (argc > 1 && !strcmp(argv[1], "dump")) {
		dump_mode = 1;
		wait_sec = argc > 2 ? atoi(argv[2]) : 30;
		if (argc > 3) {
			if (strlen(argv[3]) != 12 || hex2bin(argv[3], key, 6) != 6) {
				fprintf(stderr, "key must be 12 hex chars\n");
				return 1;
			}
			have_key = 1;
		}
	} else if (argc > 1) {
		wait_sec = atoi(argv[1]);
	}

	if (!dump_mode) {
		int s, tidx;
		if (open_target(wait_sec, &s, &tidx)) return 1;
		char line[512];
		uint8_t buf[512];
		for (;;) {
			printf("tx> ");
			if (!fgets(line, sizeof(line), stdin)) break;
			int l = hex2bin(line, buf, sizeof(buf));
			if (l == 0) break;
			if (send(s, buf, l, 0) < 0) { perror("send"); break; }
			uint8_t rbuf[4096];
			struct timeval tv = { .tv_sec = 3 };
			setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
			int r = recv(s, rbuf, sizeof(rbuf), 0);
			if (r < 0) { printf("recv: %s\n", strerror(errno)); continue; }
			printf("rx %d:", r);
			for (int i = 0; i < r; i++) printf(" %02x", rbuf[i]);
			printf("\n");
		}
		close(s);
		return 0;
	}

	static const uint8_t keys[][6] = {
		{ 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5 },
		{ 0xd3, 0xf7, 0xd3, 0xf7, 0xd3, 0xf7 },
		{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff },
		{ 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5 },
		{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
		{ 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff },
	};
	int nkeys = have_key ? 1 : (int)(sizeof(keys) / sizeof(keys[0]));
	const uint8_t (*klist)[6] = have_key ? &key : keys;

	for (int k = 0; k < nkeys; k++) {
		printf("trying key "); hexline(klist[k], 6); printf("\n");
		int ok = dump_with_key(wait_sec, klist[k]);
		if (ok == 16) { printf("full dump done\n"); return 0; }
		if (ok > 0) printf("partial dump: %d sectors\n", ok);
	}
	fprintf(stderr, "no key worked\n");
	return 1;
}
