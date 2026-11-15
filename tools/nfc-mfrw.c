// Minimal NFC client for SN100U MIFARE experiments.
// genl "nfc": DEV_UP + START_POLL, wait TARGETS_FOUND, then AF_NFC raw socket
// to the target; interactive hex TX/RX lines on stdin.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
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

static int wait_target(uint32_t *tidx, uint32_t *proto, uint8_t *uid, int *uidlen)
{
	for (;;) {
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

	uint32_t tidx = 0, proto = 0; uint8_t uid[16]; int uidlen = 0;
	if (wait_target(&tidx, &proto, uid, &uidlen)) return 1;

	int s = -1;
	struct sockaddr_nfc sn = { .sa_family = AF_NFC, .dev_idx = 0,
		.target_idx = tidx, .nfc_protocol = NFC_PROTO_MIFARE };
	for (int attempt = 0; attempt < 8 && s < 0; attempt++) {
		if (attempt) usleep(100000);
		s = socket(AF_NFC, SOCK_SEQPACKET, NFC_SOCKPROTO_RAW);
		if (s < 0) { perror("socket AF_NFC"); return 1; }
		if (connect(s, (void *)&sn, sizeof(sn)) < 0) {
			perror("connect");
			close(s); s = -1;
			continue;
		}
	}
	if (s < 0) return 1;
	printf("connected target=%u proto mask=0x%x\n", tidx, proto);

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
	return 0;
}
