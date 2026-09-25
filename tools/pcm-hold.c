/* pcm-hold: keep a PCM playback stream open for N seconds (bringup diagnostics). */
#include <tinyalsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 15;
	struct pcm_config cfg = {
		.channels = 2, .rate = 48000,
		.period_size = 1920, .period_count = 2,
		.format = PCM_FORMAT_S16_LE,
		.start_threshold = 1920, .stop_threshold = 3840,
		.silence_threshold = 0,
	};
	struct pcm *pcm = pcm_open(0, 0, PCM_OUT, &cfg);
	short buf[1920 * 2];

	if (!pcm || !pcm_is_ready(pcm)) {
		fprintf(stderr, "open fail: %s\n", pcm ? pcm_get_error(pcm) : "?");
		return 1;
	}
	memset(buf, 0, sizeof(buf));
	if (pcm_writei(pcm, buf, 1920) < 0)
		fprintf(stderr, "write1: %s\n", pcm_get_error(pcm));
	fprintf(stderr, "HOLD %d sec\n", secs);
	fflush(stderr);
	sleep(secs);
	pcm_close(pcm);
	return 0;
}
