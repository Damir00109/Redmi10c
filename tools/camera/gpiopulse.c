/* gpiopulse: drive a GPIO line low, delay, then high (power-cycle a rail).
 * usage: gpiopulse <chip> <line> [low_ms] [high_ms]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/types.h>

#define GPIO_V2_GET_LINE_IOCTL _IOWR(0xB4, 0x07, struct gpio_v2_line_request)
#define GPIO_V2_LINE_SET_VALUES_IOCTL _IOWR(0xB4, 0x0F, struct gpio_v2_line_values)
#define GPIO_V2_LINE_FLAG_OUTPUT (1ULL << 3)
#define GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES 2

struct gpio_v2_line_attribute { __u32 id; __u32 padding; __u64 values; };
struct gpio_v2_line_config_attribute {
	__u32 id; __u32 padding; __u64 values; __u64 mask;
};
struct gpio_v2_line_config {
	__u64 flags; __u32 num_attrs; __u32 padding[5];
	struct gpio_v2_line_config_attribute attrs[10];
};
struct gpio_v2_line_request {
	__u32 offsets[64]; char consumer[32];
	struct gpio_v2_line_config config;
	__u32 num_lines; __u32 event_buffer_size; __u32 padding[5]; __s32 fd;
};
struct gpio_v2_line_values { __u64 bits; __u64 mask; };

int main(int argc, char **argv)
{
	int chip = argc > 1 ? atoi(argv[1]) : 0;
	int line = argc > 2 ? atoi(argv[2]) : 26;
	int low_ms = argc > 3 ? atoi(argv[3]) : 50;
	int high_ms = argc > 4 ? atoi(argv[4]) : 20;
	char path[64];
	struct gpio_v2_line_request req;
	struct gpio_v2_line_values vals;
	int fd;

	snprintf(path, sizeof(path), "/dev/gpiochip%d", chip);
	fd = open(path, O_RDWR);
	if (fd < 0) { perror(path); return 1; }

	memset(&req, 0, sizeof(req));
	req.offsets[0] = line;
	req.num_lines = 1;
	strcpy(req.consumer, "gpiopulse");
	req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
	req.config.num_attrs = 1;
	req.config.attrs[0].id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
	req.config.attrs[0].values = 0;
	req.config.attrs[0].mask = 1;

	if (ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
		perror("get_line");
		return 1;
	}
	printf("chip%d line%d: -> LOW\n", chip, line);
	fflush(stdout);
	usleep(low_ms * 1000);

	vals.bits = 1; vals.mask = 1;
	if (ioctl(req.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals) < 0) {
		perror("set_values");
		return 1;
	}
	printf("chip%d line%d: -> HIGH (after %d ms low)\n", chip, line, low_ms);
	usleep(high_ms * 1000);
	return 0;
}
