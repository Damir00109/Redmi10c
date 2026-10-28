/*
 * libQSEEComAPI shim for tracing the vendor fingerprint HAL.
 *
 * Replaces /vendor/lib64/libQSEEComAPI.so (Magisk overlay) and forwards every
 * call to the real library, which lives next to it as libQSEEComR.so with a
 * patched SONAME.  Logs the request/response buffers and the app's shared
 * buffer (where the TA config lives) to logcat under "QSEE-SHIM".
 */
#include <stdint.h>

extern int __android_log_print(int prio, const char *tag, const char *fmt, ...);
extern void *dlopen(const char *path, int mode);
extern void *dlsym(void *handle, const char *symbol);
extern int getpid(void);

#define REAL_LIB "/vendor/lib64/libQSEEComR.so"

#define SYS_openat 56
#define SYS_close 57
#define SYS_write 64
#define AT_FDCWD (-100)

static long sc3(long n, long a, long b, long c)
{
	register long x0 asm("x0") = a;
	register long x1 asm("x1") = b;
	register long x2 asm("x2") = c;
	register long x8 asm("x8") = n;

	asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
	return x0;
}

static long sc4(long n, long a, long b, long c, long d)
{
	register long x0 asm("x0") = a;
	register long x1 asm("x1") = b;
	register long x2 asm("x2") = c;
	register long x3 asm("x3") = d;
	register long x8 asm("x8") = n;

	asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x8) : "memory");
	return x0;
}

static void put_s(char **p, const char *s)
{
	while (*s)
		*(*p)++ = *s++;
}

static void put_x(char **p, unsigned long v)
{
	static const char h[] = "0123456789abcdef";
	int i;

	put_s(p, "0x");
	for (i = (int)(sizeof(v) * 8) - 4; i >= 0; i -= 4)
		*(*p)++ = h[(v >> i) & 0xf];
}

static void put_d(char **p, long v)
{
	char tmp[24];
	int n = 0;

	if (v < 0) {
		*(*p)++ = '-';
		v = -v;
	}
	if (!v)
		tmp[n++] = '0';
	while (v) {
		tmp[n++] = '0' + (v % 10);
		v /= 10;
	}
	while (n)
		*(*p)++ = tmp[--n];
}

#define LOG(...) logf(__VA_ARGS__)
static void logf(const char *fmt, ...);

/* libQSEEComAPI handle: [0]=sb vaddr, [8]=sb size, [16]=/dev/qseecom fd */
struct qh {
	void *sb;
	uint32_t size;
	uint32_t pad0;
	int32_t fd;
	uint32_t pad1;
	uint32_t pad2;
	uint32_t ver;
};

__attribute__((constructor)) static void shim_init(void)
{
	LOG("shim loaded, pid=%d", (int)getpid());
}

static void logf(const char *fmt, ...)
{
	char buf[512];
	char *p = buf;
	const char *f = fmt;
	__builtin_va_list ap;
	int fd;

	__builtin_va_start(ap, fmt);
	while (*f && p < buf + sizeof(buf) - 32) {
		if (*f != '%') {
			*p++ = *f++;
			continue;
		}
		f++;
		if (*f == '%') {
			*p++ = '%';
			f++;
			continue;
		}
		/* skip flags, width, precision and length modifiers */
		while (*f == '0' || *f == '-' || *f == '+' || *f == ' ' ||
		       *f == '#' || *f == '.' || *f == 'l' || *f == 'z' ||
		       *f == 'h' || (*f >= '0' && *f <= '9'))
			f++;
		switch (*f++) {
		case 's': {
			const char *s = __builtin_va_arg(ap, const char *);

			put_s(&p, s ? s : "(null)");
			break;
		}
		case 'x':
			put_x(&p, __builtin_va_arg(ap, unsigned long));
			break;
		case 'd': {
			long v = __builtin_va_arg(ap, int);

			put_d(&p, v);
			break;
		}
		case 'p':
			put_x(&p, (unsigned long)__builtin_va_arg(ap, void *));
			break;
		default:
			break;
		}
	}
	__builtin_va_end(ap);
	*p++ = '\n';
	fd = (int)sc4(SYS_openat, AT_FDCWD, (long)"/data/local/tmp/qsee-trace.log",
		      01 | 0100 | 02000, 0666);
	if (fd < 0)
		return;
	sc3(SYS_write, fd, (long)buf, p - buf);
	sc3(SYS_close, fd, 0, 0);
}

static void *real_sym(const char *name)
{
	static void *lib;
	void *sym;

	if (!lib)
		lib = dlopen(REAL_LIB, 2 /* RTLD_NOW */);
	if (!lib) {
		LOG("cannot open %s", REAL_LIB);
		return 0;
	}
	sym = dlsym(lib, name);
	if (!sym)
		LOG("no symbol %s", name);
	return sym;
}

static void dump(const char *tag, const void *p, uint32_t len)
{
	const unsigned char *b = p;
	char line[16 * 3 + 1];
	uint32_t off, i, n;

	if (!p || !len)
		return;
	if (len > 0x100)
		len = 0x100;
	for (off = 0; off < len; off += 16) {
		n = len - off > 16 ? 16 : len - off;
		for (i = 0; i < n; i++) {
			static const char hx[] = "0123456789abcdef";

			line[i * 3] = hx[b[off + i] >> 4];
			line[i * 3 + 1] = hx[b[off + i] & 15];
			line[i * 3 + 2] = ' ';
		}
		line[n * 3] = 0;
		LOG("%s +%03x: %s", tag, off, line);
	}
}

static void dump_handle(const char *tag, void *h)
{
	struct qh *q = h;

	if (!q)
		return;
	LOG("%s handle sb=%p size=0x%x fd=%d", tag, q->sb, q->size, q->fd);
	if (q->sb && q->size && q->size <= 0x10000)
		dump(tag, q->sb, q->size);
}

int QSEECom_start_app(void **handle, const char *path, const char *name,
		      uint32_t size)
{
	int (*f)(void **, const char *, const char *, uint32_t);
	int ret;

	f = real_sym("QSEECom_start_app");
	LOG("start_app path=%s name=%s size=0x%x", path, name, size);
	ret = f ? f(handle, path, name, size) : -1;
	LOG("start_app -> %d", ret);
	if (!ret && handle)
		dump_handle("sb@start", *handle);
	return ret;
}

int QSEECom_start_app_V2(void **handle, const char *path, const char *name,
			 uint32_t size, uint32_t arch)
{
	int (*f)(void **, const char *, const char *, uint32_t, uint32_t);
	int ret;

	f = real_sym("QSEECom_start_app_V2");
	LOG("start_app_V2 path=%s name=%s size=0x%x arch=%d", path, name, size, arch);
	ret = f ? f(handle, path, name, size, arch) : -1;
	LOG("start_app_V2 -> %d", ret);
	if (!ret && handle)
		dump_handle("sb@startV2", *handle);
	return ret;
}

int QSEECom_load_external_elf(void **handle, const char *path, const char *name,
			      uint32_t size)
{
	int (*f)(void **, const char *, const char *, uint32_t);
	int ret;

	f = real_sym("QSEECom_load_external_elf");
	LOG("load_external_elf name=%s size=0x%x", name, size);
	ret = f ? f(handle, path, name, size) : -1;
	LOG("load_external_elf -> %d", ret);
	return ret;
}

int QSEECom_unload_external_elf(void **handle)
{
	int (*f)(void **);

	f = real_sym("QSEECom_unload_external_elf");
	LOG("unload_external_elf");
	return f ? f(handle) : -1;
}

int QSEECom_register_listener(void **handle, uint32_t listener_id, uint32_t sb_size)
{
	int (*f)(void **, uint32_t, uint32_t);

	f = real_sym("QSEECom_register_listener");
	LOG("register_listener id=%d sb=0x%x", listener_id, sb_size);
	return f ? f(handle, listener_id, sb_size) : -1;
}

int QSEECom_unregister_listener(void *handle, uint32_t listener_id)
{
	int (*f)(void *, uint32_t);

	f = real_sym("QSEECom_unregister_listener");
	return f ? f(handle, listener_id) : -1;
}

int QSEECom_receive_req(void *handle)
{
	int (*f)(void *);

	f = real_sym("QSEECom_receive_req");
	return f ? f(handle) : -1;
}

int QSEECom_send_resp(void *handle)
{
	int (*f)(void *);

	f = real_sym("QSEECom_send_resp");
	return f ? f(handle) : -1;
}

int QSEECom_get_app_info(void *handle, void *info)
{
	int (*f)(void *, void *);

	f = real_sym("QSEECom_get_app_info");
	LOG("get_app_info");
	return f ? f(handle, info) : -1;
}

int QSEECom_send_modified_cmd_64(void *handle, void *send, uint32_t send_len,
				 void *rsp, uint32_t rsp_len, void *ifd)
{
	int (*f)(void *, void *, uint32_t, void *, uint32_t, void *);
	int ret;

	f = real_sym("QSEECom_send_modified_cmd_64");
	if (send && send_len >= 4)
		LOG("mod64 cmd=0x%x len=%u", *(uint32_t *)send, send_len);
	dump("mod64 req", send, send_len);
	dump_handle("sb@mod64-before", handle);
	ret = f ? f(handle, send, send_len, rsp, rsp_len, ifd) : -1;
	LOG("mod64 -> %d", ret);
	dump("mod64 rsp", rsp, rsp_len);
	dump_handle("sb@mod64-after", handle);
	return ret;
}

int QSEECom_set_bandwidth(void *handle, uint8_t enable)
{
	int (*f)(void *, uint8_t);

	f = real_sym("QSEECom_set_bandwidth");
	return f ? f(handle, enable) : -1;
}

int QSEECom_app_load_query(void *handle, const char *name)
{
	int (*f)(void *, const char *);

	f = real_sym("QSEECom_app_load_query");
	LOG("app_load_query name=%s", name);
	return f ? f(handle, name) : -1;
}

int QSEECom_shutdown_app(void **handle)
{
	int (*f)(void **);

	f = real_sym("QSEECom_shutdown_app");
	LOG("shutdown_app");
	return f ? f(handle) : -1;
}

int QSEECom_send_cmd(void *handle, void *send, uint32_t send_len, void *rsp,
		     uint32_t rsp_len)
{
	int (*f)(void *, void *, uint32_t, void *, uint32_t);
	int ret;

	f = real_sym("QSEECom_send_cmd");
	if (send && send_len >= 4)
		LOG("cmd req cmd=0x%x len=%u", *(uint32_t *)send, send_len);
	dump("cmd req", send, send_len);
	dump_handle("sb@cmd-before", handle);
	ret = f ? f(handle, send, send_len, rsp, rsp_len) : -1;
	LOG("cmd -> %d", ret);
	dump("cmd rsp", rsp, rsp_len);
	dump_handle("sb@cmd-after", handle);
	return ret;
}

int QSEECom_send_modified_cmd(void *handle, void *send, uint32_t send_len,
			      void *rsp, uint32_t rsp_len, void *ifd)
{
	int (*f)(void *, void *, uint32_t, void *, uint32_t, void *);
	int ret;

	f = real_sym("QSEECom_send_modified_cmd");
	if (send && send_len >= 4)
		LOG("mod cmd=0x%x len=%u", *(uint32_t *)send, send_len);
	dump("mod req", send, send_len);
	dump_handle("sb@mod-before", handle);
	ret = f ? f(handle, send, send_len, rsp, rsp_len, ifd) : -1;
	LOG("mod -> %d", ret);
	dump("mod rsp", rsp, rsp_len);
	dump_handle("sb@mod-after", handle);
	return ret;
}
