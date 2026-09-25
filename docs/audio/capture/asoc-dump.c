/* Minimal ALSA control dump — static build for Android/arm64.
 * Dumps every mixer control of /dev/snd/controlC0 with values.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <sound/asound.h>

static const char *type_name(int t)
{
	switch (t) {
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN: return "BOOLEAN";
	case SNDRV_CTL_ELEM_TYPE_INTEGER: return "INTEGER";
	case SNDRV_CTL_ELEM_TYPE_ENUMERATED: return "ENUM";
	case SNDRV_CTL_ELEM_TYPE_BYTES: return "BYTES";
	case SNDRV_CTL_ELEM_TYPE_IEC958: return "IEC958";
	case SNDRV_CTL_ELEM_TYPE_INTEGER64: return "INTEGER64";
	}
	return "?";
}

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/snd/controlC0";
	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }

	struct snd_ctl_card_info ci;
	memset(&ci, 0, sizeof(ci));
	ioctl(fd, SNDRV_CTL_IOCTL_CARD_INFO, &ci);
	printf("# card%d: %s (%s) driver=%s\n", ci.card, ci.name, ci.id, ci.driver);

	struct snd_ctl_elem_list el;
	memset(&el, 0, sizeof(el));
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &el) < 0) { perror("ELEM_LIST"); return 1; }
	printf("# controls: %u\n", el.count);
	el.pids = calloc(el.count ? el.count : 1, sizeof(struct snd_ctl_elem_id));
	el.space = el.count;
	el.offset = 0;
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &el) < 0) { perror("ELEM_LIST2"); return 1; }

	for (unsigned i = 0; i < el.used; i++) {
		struct snd_ctl_elem_info ei;
		memset(&ei, 0, sizeof(ei));
		ei.id = el.pids[i];
		if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &ei) < 0)
			continue;

		struct snd_ctl_elem_value ev;
		memset(&ev, 0, sizeof(ev));
		ev.id = ei.id;
		int rd = ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, &ev);

		printf("%s:", ei.id.name);
		if (rd < 0) { printf(" <read err>\n"); continue; }
		if (ei.type == SNDRV_CTL_ELEM_TYPE_ENUMERATED) {
			/* fetch name of the selected enum item */
			struct snd_ctl_elem_info en;
			memset(&en, 0, sizeof(en));
			en.id = ei.id;
			en.value.enumerated.item = ev.value.enumerated.item[0];
			const char *ename = "?";
			if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &en) == 0)
				ename = en.value.enumerated.name;
			printf(" enum[%u]=%u (%s)\n", ei.value.enumerated.items,
			       ev.value.enumerated.item[0], ename);
		} else if (ei.type == SNDRV_CTL_ELEM_TYPE_BYTES) {
			printf(" bytes[%u]=", ei.count);
			for (unsigned j = 0; j < ei.count && j < 32; j++)
				printf("%02x", ev.value.bytes.data[j]);
			printf("\n");
		} else {
			printf(" %s[%u]=", type_name(ei.type), ei.count);
			for (unsigned j = 0; j < ei.count && j < 8; j++)
				printf("%ld ", ev.value.integer.value[j]);
			printf("\n");
		}
	}
	return 0;
}
