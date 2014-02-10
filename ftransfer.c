/*
 * transfer.c - transfer PCM samples with ALSA/FFADO backend
 *
 * This program is just for profiling,
 * not comparing their advantages/disadvantages.
 *
 * gcc ./ftransfer.c -lasound -lffado -lm -o ./ftransfer
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <signal.h>
#include <getopt.h>

#include <alsa/asoundlib.h>
#include "include/uapi/sound/firewire.h"

#include <libffado/ffado.h>

bool run;

enum driver_type {
	DRIVER_ALSA,
	DRIVER_FFADO
};

struct something {
	enum driver_type driver;
	unsigned int card;
	unsigned int rtprio;
	unsigned char guid[8];
	char sdev[16];
	char fdev[16];

	unsigned int bits_per_sample;
	unsigned int samples_per_frame;
	unsigned int frames_per_period;
	unsigned int periods_per_buffer;
	unsigned int frames_per_second;
	unsigned int seconds;
	uint8_t *buffer;

	unsigned int verbose;
};

static int
keep_buffer(struct something *opts)
{
	unsigned int s, f, bytes_per_sample, mask;

	mask = (1 << opts->bits_per_sample) - 1;
	bytes_per_sample = opts->bits_per_sample / 8;

	/* This buffer is used for all of channels */
	opts->buffer = malloc(bytes_per_sample * opts->samples_per_frame * opts->frames_per_period);
	if (opts->buffer == NULL)
		return -ENOMEM;

	/*
	 * NOTE:
	 * ALSA driver uses interleaved buffer.
	 * FFADO driver uses non-interleaved buffer.
	 * Here don't mind them and use random PCM samples.
	 */
	for (f = 0; f < opts->frames_per_period; f++) {
		opts->buffer[f] = random() & mask;
		for (s = 0; s < opts->samples_per_frame; s++)
			opts->buffer[f + s * bytes_per_sample * opts->frames_per_period] = opts->buffer[f];
	}

	return 0;
}

static int
card_open(void **handle, struct something *opts)
{
	int err = 0;

	if (opts->driver == DRIVER_ALSA) {
		err = snd_pcm_open((snd_pcm_t **)handle, opts->sdev,
				   SND_PCM_STREAM_PLAYBACK,
				   SND_PCM_NO_AUTO_RESAMPLE |
				   SND_PCM_NO_AUTO_CHANNELS |
				   SND_PCM_NO_AUTO_FORMAT);
	} else {
		char target[16] = {0};
		char *strings[1];
		ffado_device_info_t info = {0};
		ffado_options_t options = {0};
		unsigned int i;

		if (false) {
			/*
			 * libffado svn 2478 has a bug to match guids.
			 * strtoll() can return 64bit value in C99/C++11 or later.
			 */
			target[0] = 'g';
			target[1] = 'u';
			target[2] = 'i';
			target[3] = 'd';
			target[4] = ':';
			target[5] = opts->guid[0];
			target[6] = opts->guid[1];
			target[7] = opts->guid[2];
			target[8] = opts->guid[3];
			target[9] = opts->guid[4];
			target[10] = opts->guid[5];
			target[11] = opts->guid[6];
			target[12] = opts->guid[7];
		} else {
			/* use port 0. node id is picked up from 'fw%d' */
			snprintf(target, sizeof(target), "hw:0,%s", opts->fdev + 2);
		}

		strings[0] = target;
		info.nb_device_spec_strings = 1;
		info.device_spec_strings = strings;

		options.verbose = opts->verbose;
		options.sample_rate = opts->frames_per_second;
		/* buffer params */
		options.period_size = opts->frames_per_period;
		options.nb_buffers = opts->periods_per_buffer;
		/* params of threads for packetization */
		options.realtime = (opts->rtprio > 0);
		options.packetizer_priority = opts->rtprio;
		/*
		 * These are options for synchronization of multi devices on
		 * the same IEEE 1394 bus. There are some issues of
		 * inter-operability.
		 */
		options.slave_mode = 0;
		options.snoop_mode = 0;

		*handle = ffado_streaming_init(info, options);
		if (*handle == NULL)
			err = -EINVAL;
	}

	return err;
}

static int
card_hw_params(void *handle, struct something *opts)
{
	int err;

	if (opts->driver == DRIVER_ALSA) {
		snd_pcm_t *snd = handle;
		snd_pcm_hw_params_t *params;
		snd_pcm_format_t format;

		snd_pcm_hw_params_alloca(&params);

		err = snd_pcm_hw_params_any(snd, params);
		if (err < 0)
			goto end;

		err = snd_pcm_hw_params_set_access(snd, params,
					SND_PCM_ACCESS_RW_INTERLEAVED);
		if (err < 0)
			goto end;

		err = snd_pcm_hw_params_set_rate(snd, params,
				opts->frames_per_second, SND_PCM_STREAM_PLAYBACK);
		if (err < 0)
			goto end;

		err = snd_pcm_hw_params_set_period_size(snd, params,
			opts->frames_per_period, SND_PCM_STREAM_PLAYBACK);
		if (err < 0)
			goto end;

		err = snd_pcm_hw_params_set_buffer_size(snd, params,
			opts->frames_per_period * opts->periods_per_buffer);
		if (err < 0)
			goto end;

		/* snd_pcm_prepare() is also called in this function. */
		err = snd_pcm_hw_params(snd, params);
		if (err < 0)
			goto end;

		err = snd_pcm_hw_params_get_format(params, &format);
		if (err < 0)
			goto end;
		opts->bits_per_sample = snd_pcm_format_width(format);

		err = snd_pcm_hw_params_get_channels(params,
						     &opts->samples_per_frame);
		if (err < 0)
			goto end;

		err = keep_buffer(opts);
	} else {
		ffado_device_t *ffado = handle;
		unsigned int ch, data_channels;

		/* In in-stream, use none of data channels */
		data_channels = ffado_streaming_get_nb_capture_streams(ffado);
		for (ch = 0; ch < data_channels; ch++)
			ffado_streaming_capture_stream_onoff(ffado, ch, 0);

		/* In out-stream, use channels for PCM samples */
		opts->samples_per_frame = 0;
		data_channels = ffado_streaming_get_nb_playback_streams(ffado);
		for (ch = 0; ch < data_channels; ch++) {
			switch (ffado_streaming_get_playback_stream_type(ffado, ch)) {
			case ffado_stream_type_audio:
				ffado_streaming_playback_stream_onoff(ffado, ch, 1);
				opts->samples_per_frame++;
				break;
			default:
				ffado_streaming_playback_stream_onoff(ffado, ch, 0);
				break;
			}
		}

		/* transfer 24 bit sample */
		ffado_streaming_set_audio_datatype(ffado, ffado_audio_datatype_int24);
		opts->bits_per_sample = 24;

		err = ffado_streaming_set_period_size(ffado, opts->frames_per_period);
		if (err < 0)
			goto end;

		err = keep_buffer(opts);
		if (err < 0)
			goto end;

		/* set buffer */
		data_channels = ffado_streaming_get_nb_playback_streams(ffado);
		for (ch = 0; ch < data_channels; ch++) {
			switch (ffado_streaming_get_playback_stream_type(ffado, ch)) {
			case ffado_stream_type_audio:
				ffado_streaming_set_playback_stream_buffer(ffado, ch,
					opts->buffer + ch * opts->frames_per_period + opts->bits_per_sample / 8);
				break;
			default:
				break;
			}
		}

		err = ffado_streaming_prepare(ffado);
		if (err < 0)
			goto end;

		err = ffado_streaming_start(ffado);
	}
end:
	return err;
}

static int
card_process(void *handle, struct something *opts)
{
	unsigned int samples_per_frame, s, max_frames, total_frames;
	int frames, err = 0;

	max_frames = opts->frames_per_second * opts->seconds;
	total_frames = 0;

	run = true;
	while (run && (total_frames < max_frames)) {
		frames = opts->frames_per_period;

		if (opts->driver == DRIVER_ALSA) {
			snd_pcm_t *snd = handle;

			while (frames > 0) {
				err = snd_pcm_writei(snd, opts->buffer, frames);
				if (err == -EAGAIN) {
					continue;
				} else {
					if (err == -EPIPE)
						err = snd_pcm_prepare(snd);
					if (err < 0)
						goto end;
				}
				frames -= err;
				total_frames += err;
			}
		} else {
			ffado_device_t *ffado = handle;

			err = ffado_streaming_wait(ffado);
			switch (err) {
			case ffado_wait_xrun:
				err = ffado_streaming_reset(ffado);
				if (err < 0)
					goto end;
				break;
			case ffado_wait_shutdown:
			case ffado_wait_error:
				goto end;
			case ffado_wait_ok:
			default:
				break;
			}

			ffado_streaming_transfer_capture_buffers(ffado);
			ffado_streaming_transfer_playback_buffers(ffado);

			total_frames += frames;
		}
	}
end:
	return err;
}

static void
card_close(void *handle, struct something *opts)
{
	if (opts->buffer != NULL)
		free(opts->buffer);

	if (opts->driver == DRIVER_ALSA) {
		/* snd_pcm_hw_free() is also called in this function. */
		snd_pcm_close((snd_pcm_t *)handle);
	} else {
		ffado_streaming_stop((ffado_device_t *)handle);
		ffado_streaming_finish((ffado_device_t *)handle);
	}
}

static int
get_first_card(struct something *opts)
{
	struct snd_firewire_get_info info = {0};
	snd_hwdep_t *hw;
	int number, err;
	char buf[6];

	number = -1;
	do {
		err = snd_card_next(&number);
		if (err < 0)
			break;
		if (number < 0)
			break;

		sprintf(buf, "hw:%d", number);
		err = snd_hwdep_open(&hw, buf, 0);
		if (err < 0)
			continue;

		err = snd_hwdep_ioctl(hw, SNDRV_FIREWIRE_IOCTL_GET_INFO,
				      (void *)&info);
		if (err >= 0) {
			strcpy(opts->sdev, buf);
			memcpy(opts->guid, info.guid, sizeof(info.guid));
			memcpy(opts->fdev, info.device_name, sizeof(info.device_name));
			snd_hwdep_close(hw);
			return 0;
		}
	} while (number < 100);

	return -1;
}

static void
parse_options(int argc, char *argv[], struct something *opts)
{
	const struct option long_options[] = {
		{"driver",	1, NULL, 'd'},
		{"fps",		1, NULL, 'r'},
		{"ppb",		1, NULL, 'b'},
		{"fpp",		1, NULL, 'p'},
		{"rtprio",	1, NULL, 'i'},
		{"seconds",	1, NULL, 's'},
		{NULL,		0, NULL, 0},
	};

	/* default values */
	opts->driver = DRIVER_ALSA;
	opts->frames_per_second = 48000;
	opts->frames_per_period = 512;
	opts->periods_per_buffer = 2;
	opts->seconds = 3;
	opts->rtprio = 0;
	opts->verbose = 0;

	while (1) {
		int c;
		c = getopt_long(argc, argv, "d:r:b:p:i:s:v:", long_options, NULL);
		if (c < 0)
			break;

		switch (c) {
		case 'd':
			if (strcmp("ffado", optarg) == 0)
				opts->driver = DRIVER_FFADO;
			break;
		case 'r':
			opts->frames_per_second = atoi(optarg);
			break;
		case 'b':
			opts->periods_per_buffer = atoi(optarg);
			break;
		case 'p':
			opts->frames_per_period = atoi(optarg);
			break;
		case 'i':
			opts->rtprio = atoi(optarg);
			break;
		case 's':
			opts->seconds = atoi(optarg);
			break;
		case 'v':
			opts->verbose = atoi(optarg);
			break;
		}
	}
}

static void signal_handler(int sig)
{
	run = false;
}

int main(int argv, char *argc[])
{
	struct something opts = {0};
	void *handle;
	int err;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGABRT, signal_handler);

	parse_options(argv, argc, &opts);

	err = get_first_card(&opts);
	if (err < 0)
		goto end;

	/* open character device */
	err = card_open(&handle, &opts);
	if (err < 0)
		goto end;

	/* set parameters and start streams */
	err = card_hw_params(handle, &opts);
	if (err < 0)
		goto close;

	/* transfer PCM samples */
	err = card_process(handle, &opts);
close:
	/* stop streams and close character devices */
	if (handle != NULL)
		card_close(handle, &opts);
end:
	if (err < 0)
		printf("Error :%s\n", snd_strerror(err));
	exit(EXIT_SUCCESS);
}
