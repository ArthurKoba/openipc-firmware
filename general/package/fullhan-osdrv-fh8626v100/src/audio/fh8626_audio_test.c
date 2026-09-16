#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* Exact FH8626V100 stock ACW transport recovered from Apollo V2.1.0.P14. */
#define RTXBUS_RESET 0x40000000UL
#define RTXBUS_COMMAND 0x20000000UL
#define AC_CMD_AI_ENABLE 6U
#define AC_CMD_AI_DISABLE 7U
#define AC_CMD_AI_VOLUME 15U
#define AC_CMD_AO_ENABLE 8U
#define AC_CMD_AO_DISABLE 10U
#define NO_SPEAKER_MUTE_GPIO 255U

struct ac_init_command {
	uint32_t size;
	uint16_t size_a;
	uint16_t size_b;
	uint32_t opcode;
	int32_t status;
	uint32_t reserved;
	uint32_t map_offset;
	uint32_t map_length;
	uint32_t tail_length;
};

struct ac_simple_command {
	uint32_t size;
	uint16_t size_a;
	uint16_t size_b;
	uint32_t opcode;
	int32_t status;
	uint32_t value;
};

struct ac_config_command {
	uint32_t size;
	uint16_t size_a;
	uint16_t size_b;
	uint32_t opcode;
	int32_t status;
	uint32_t config[7];
	uint32_t selector;
};

struct ac_frame_command {
	uint32_t size;
	uint16_t size_a;
	uint16_t size_b;
	uint32_t opcode;
	int32_t status;
	uint32_t data_length;
	uint32_t data_offset;
	uint32_t pts_low;
	uint32_t pts_high;
};

struct ac_init_params_command {
	uint32_t size;
	uint16_t size_a;
	uint16_t size_b;
	uint32_t opcode;
	int32_t status;
	uint32_t reserved;
	uint32_t blob_length;
	uint8_t blob[0x17a];
};

struct ac_pair_command {
	uint32_t size;
	uint16_t size_a;
	uint16_t size_b;
	uint32_t opcode;
	int32_t status;
	uint32_t values[2];
};

/* Exact AJL33PQ0866 AC init parameters captured from stock Apollo at 0x31af90. */
static const uint8_t retail_init_params[0x17a] = {
	0x03, 0x00, 0xe6, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0xd8, 0xff,
	0xec, 0xff, 0xf4, 0xff, 0xf4, 0xff, 0xf4, 0xff, 0xf4, 0xff, 0xfd, 0xff,
	0x11, 0x00, 0x12, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0xc4, 0xff, 0xfc, 0xff, 0xfe, 0xff, 0xf4, 0xff, 0xf6, 0xff,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0xb0, 0xff, 0xb0, 0xff, 0x0f, 0x00, 0xc8, 0x00, 0xc8, 0x00,
	0x2c, 0x01, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04,
	0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0x58, 0x02, 0x58, 0x02, 0x58, 0x02,
	0x58, 0x02, 0x58, 0x02, 0x58, 0x02, 0x58, 0x02, 0x58, 0x02, 0x58, 0x02,
	0x58, 0x02, 0x58, 0x02, 0xe8, 0x03, 0xe8, 0x03, 0xe8, 0x03, 0xe8, 0x03,
	0xe8, 0x03, 0xe8, 0x03, 0xe8, 0x03, 0xe8, 0x03, 0xe8, 0x03, 0xbc, 0x02,
	0xbc, 0x02, 0xbc, 0x02, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04,
	0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04,
	0xbc, 0x02, 0xbc, 0x02, 0xbc, 0x02, 0xbc, 0x02, 0xbc, 0x02, 0xbc, 0x02,
	0xbc, 0x02, 0xbc, 0x02, 0xbc, 0x02, 0xbc, 0x02, 0xbc, 0x02, 0x20, 0x03,
	0x20, 0x03, 0x20, 0x03, 0x20, 0x03, 0x20, 0x03, 0x20, 0x03, 0x20, 0x03,
	0x20, 0x03, 0x20, 0x03, 0x32, 0x00, 0x32, 0x00, 0xc8, 0x00, 0xf4, 0x01,
	0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04,
	0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04,
	0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04,
	0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04,
	0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04,
	0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04,
	0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04,
	0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04,
	0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04,
	0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04, 0xb0, 0x04,
	0xb0, 0x04, 0x03, 0x00, 0x01, 0x00, 0x36, 0x0e, 0x76, 0x14, 0xb4, 0x10,
	0x5e, 0x18, 0x00, 0x00, 0x01, 0x00, 0x84, 0x18, 0x04, 0x00, 0x08, 0x00,
	0x02, 0x00, 0x0c, 0x00, 0x02, 0x00, 0x00, 0x00, 0x23, 0x00, 0x19, 0x00,
	0x0f, 0x00, 0x14, 0x00, 0x01, 0x00, 0x28, 0x4b, 0x88, 0x3b, 0x00, 0x00,
	0x0c, 0x00, 0x64, 0x00, 0x40, 0x1f,
};

static int fail(const char *what)
{
	fprintf(stderr, "%s: %s\n", what, strerror(errno));
	return 1;
}

static int ac_simple(int fd, uint32_t command, uint32_t value)
{
	struct ac_simple_command request = {
		.size = 12,
		.size_a = 12,
		.size_b = 12,
		.opcode = 0x01000000U | command |
			(command == 3U ? 0U : 0x00040000U),
		.value = value,
	};

	if (ioctl(fd, RTXBUS_COMMAND, &request) < 0)
		return -1;
	if (request.status != 0) {
		fprintf(stderr, "AC simple command %u status=0x%08x\n",
			command, (uint32_t)request.status);
		errno = EIO;
		return -1;
	}
	return 0;
}

static int ac_set_init_params(int fd)
{
	struct ac_init_params_command request = {
		.size = 0x18a,
		.size_a = 0x18a,
		.size_b = 0x18a,
		.opcode = 0x01040022,
		.reserved = 0,
		.blob_length = sizeof(retail_init_params),
	};

	memcpy(request.blob, retail_init_params, sizeof(retail_init_params));
	if (ioctl(fd, RTXBUS_COMMAND, &request) < 0)
		return -1;
	if (request.status != 0) {
		fprintf(stderr, "AC init-params status=0x%08x\n",
			(uint32_t)request.status);
		errno = EIO;
		return -1;
	}
	return 0;
}

static int ac_set_capture_nr(int fd, uint32_t enabled, uint32_t level)
{
	struct ac_pair_command request = {
		.size = 16,
		.size_a = 16,
		.size_b = 16,
		.opcode = 0x01040013,
		.values = { enabled, level },
	};

	if (ioctl(fd, RTXBUS_COMMAND, &request) < 0)
		return -1;
	if (request.status != 0) {
		fprintf(stderr, "AC capture-NR status=0x%08x\n",
			(uint32_t)request.status);
		errno = EIO;
		return -1;
	}
	return 0;
}

static int ac_set_retail_config(int fd, uint32_t type, uint32_t sample_rate,
	uint32_t packet_bytes, uint32_t volume)
{
	struct ac_config_command request = {
		.size = 8,
		.size_a = 8,
		.size_b = 0x28,
		.opcode = 0x01040005,
		/* type, sample rate, width, encoding, channels, packet, volume */
		.config = { type, sample_rate, 16, 0, 1, packet_bytes, volume },
		.selector = 4,
	};

	if (ioctl(fd, RTXBUS_COMMAND, &request) < 0)
		return -1;
	if (request.status != 0) {
		errno = EIO;
		return -1;
	}
	return 0;
}

static long elapsed_ms(const struct timespec *start, const struct timespec *now)
{
	return (now->tv_sec - start->tv_sec) * 1000L +
		(now->tv_nsec - start->tv_nsec) / 1000000L;
}

static volatile sig_atomic_t playback_stop;
static volatile sig_atomic_t capture_stop;
static int speaker_mute_fd = -1;
static unsigned speaker_mute_gpio = NO_SPEAKER_MUTE_GPIO;
static unsigned speaker_mute_active = 1;

static unsigned env_uint(const char *name, unsigned fallback,
	unsigned minimum, unsigned maximum)
{
	const char *text = getenv(name);
	char *end;
	unsigned long value;

	if (text == NULL || *text == '\0')
		return fallback;
	errno = 0;
	value = strtoul(text, &end, 0);
	if (errno != 0 || *end != '\0' || value < minimum || value > maximum) {
		fprintf(stderr, "%s must be in %u..%u\n", name, minimum, maximum);
		exit(2);
	}
	return (unsigned)value;
}

static int supported_rate(unsigned rate)
{
	return rate == 8000U || rate == 11025U || rate == 16000U ||
		rate == 22050U || rate == 32000U || rate == 44100U ||
		rate == 48000U;
}

static int write_text_file(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY);
	ssize_t length = (ssize_t)strlen(text);
	int saved;

	if (fd < 0)
		return -1;
	if (write(fd, text, (size_t)length) != length) {
		saved = errno;
		close(fd);
		errno = saved ? saved : EIO;
		return -1;
	}
	return close(fd);
}

static int speaker_set_mute(int muted)
{
	const char value = (muted ? speaker_mute_active : !speaker_mute_active) ?
		'1' : '0';

	if (speaker_mute_gpio == NO_SPEAKER_MUTE_GPIO)
		return 0;
	if (speaker_mute_fd < 0) {
		errno = EBADF;
		return -1;
	}
	if (lseek(speaker_mute_fd, 0, SEEK_SET) < 0)
		return -1;
	return write(speaker_mute_fd, &value, 1) == 1 ? 0 : -1;
}

static void playback_signal(int signal_number)
{
	(void)signal_number;
	playback_stop = 1;
	if (speaker_mute_fd >= 0) {
		(void)lseek(speaker_mute_fd, 0, SEEK_SET);
		const char value = speaker_mute_active ? '1' : '0';
		(void)write(speaker_mute_fd, &value, 1);
	}
}

static void capture_signal(int signal_number)
{
	(void)signal_number;
	capture_stop = 1;
}

static int speaker_prepare_muted(void)
{
	unsigned attempt;
	char command[48];
	char number[16];
	char path[64];

	if (speaker_mute_gpio == NO_SPEAKER_MUTE_GPIO)
		return 0;
	/* External amplifier mute wiring is supplied by the board profile. */
	snprintf(command, sizeof(command), "mux,GPIO%u,GPIO%u,0\n",
		speaker_mute_gpio, speaker_mute_gpio);
	snprintf(number, sizeof(number), "%u\n", speaker_mute_gpio);
	snprintf(path, sizeof(path), "/sys/class/gpio/GPIO%u", speaker_mute_gpio);
	if (write_text_file("/proc/driver/pinctrl", command) < 0)
		return -1;
	if (access(path, F_OK) != 0) {
		if (write_text_file("/sys/class/gpio/export", number) < 0 &&
		    errno != EBUSY)
			return -1;
		for (attempt = 0; attempt < 50; ++attempt) {
			if (access(path, F_OK) == 0)
				break;
			usleep(10000);
		}
	}
	if (access(path, F_OK) != 0) {
		errno = ENODEV;
		return -1;
	}
	strncat(path, "/direction", sizeof(path) - strlen(path) - 1);
	if (write_text_file(path, speaker_mute_active ? "high\n" : "low\n") < 0)
		return -1;
	path[strlen(path) - strlen("direction")] = '\0';
	strncat(path, "value", sizeof(path) - strlen(path) - 1);
	speaker_mute_fd = open(path, O_WRONLY);
	if (speaker_mute_fd < 0)
		return -1;
	return speaker_set_mute(1);
}

static int ac_send_ao_frame(int fd, uint8_t *shared,
	const struct ac_init_command *init, const int16_t *samples, uint32_t bytes)
{
	struct ac_frame_command frame = {
		.size = 16,
		.size_a = 16,
		.size_b = 16,
		.opcode = 0x01008002,
		.data_length = bytes,
	};
	uint32_t relative;

	if ((bytes & 1U) != 0 || bytes == 0 || init->tail_length < bytes ||
	    init->tail_length > init->map_length) {
		errno = EINVAL;
		return -1;
	}
	relative = init->map_length - init->tail_length;
	memcpy(shared + relative, samples, bytes);
	frame.data_offset = init->map_offset + relative;
	if (ioctl(fd, RTXBUS_COMMAND, &frame) < 0)
		return -1;
	if (frame.status != 0) {
		fprintf(stderr, "AC AO frame status=0x%08x\n",
			(uint32_t)frame.status);
		errno = EIO;
		return -1;
	}
	return 0;
}

static int playback_test(const char *pcm_path)
{
	static const int16_t sine_q15[64] = {
		0, 3212, 6393, 9512, 12539, 15446, 18204, 20787,
		23170, 25330, 27245, 28898, 30273, 31356, 32137, 32610,
		32767, 32610, 32137, 31356, 30273, 28898, 27245, 25330,
		23170, 20787, 18204, 15446, 12539, 9512, 6393, 3212,
		0, -3212, -6393, -9512, -12539, -15446, -18204, -20787,
		-23170, -25330, -27245, -28898, -30273, -31356, -32137, -32610,
		-32767, -32610, -32137, -31356, -30273, -28898, -27245, -25330,
		-23170, -20787, -18204, -15446, -12539, -9512, -6393, -3212
	};
	static const struct {
		uint16_t frequency;
		uint8_t frames;
	} melody[] = {
		{659, 7}, {659, 7}, {0, 7}, {659, 7}, {0, 7},
		{523, 7}, {659, 14}, {784, 14}, {0, 14}, {392, 14}, {0, 14},
		{523, 14}, {0, 7}, {392, 14}, {0, 7}, {330, 14}, {0, 7},
		{440, 7}, {494, 7}, {466, 7}, {440, 14}
	};
	struct ac_init_command init = {
		.size = 0x18,
		.size_a = 0x18,
		.size_b = 0x18,
		.opcode = 0x01040004,
	};
	int16_t packet[1024];
	uint8_t *shared = MAP_FAILED;
	int audio_fd = -1;
	int pcm_fd = -1;
	int ao_enabled = 0;
	int rc = 1;
	uint32_t phase = 0;
	uint32_t phase_step;
	unsigned note_index;
	unsigned note_frame;
	unsigned sample_index;
	unsigned played_frames = 0;
	unsigned sample_rate = env_uint("FH8626_AUDIO_PLAYBACK_RATE", 16000,
		8000, 48000);
	unsigned packet_bytes = env_uint("FH8626_AUDIO_PLAYBACK_PACKET", 320,
		32, 2048);
	unsigned volume = env_uint("FH8626_AUDIO_PLAYBACK_VOLUME", 31, 0, 31);
	unsigned packet_samples;
	unsigned frame_us;
	unsigned frame_limit;
	int pcm_stream = pcm_path != NULL && strcmp(pcm_path, "-") == 0;
	ssize_t pcm_bytes;

	if (!supported_rate(sample_rate)) {
		fprintf(stderr, "unsupported playback rate %u\n", sample_rate);
		return 2;
	}
	if ((packet_bytes & 3U) != 0) {
		fprintf(stderr, "playback packet must be divisible by 4\n");
		return 2;
	}
	packet_samples = packet_bytes / sizeof(packet[0]);
	frame_us = (unsigned)(((uint64_t)packet_samples * 1000000U) / sample_rate);
	frame_limit = frame_us == 0 ? 1 : 60000000U / frame_us;
	speaker_mute_gpio = env_uint("FH8626_AUDIO_MUTE_GPIO",
		NO_SPEAKER_MUTE_GPIO, 0, NO_SPEAKER_MUTE_GPIO);
	speaker_mute_active = env_uint("FH8626_AUDIO_MUTE_ACTIVE", 1, 0, 1);

	playback_stop = 0;
	if (speaker_prepare_muted() < 0)
		return fail("prepare external speaker mute");
	signal(SIGINT, playback_signal);
	signal(SIGTERM, playback_signal);

	audio_fd = open("/dev/rtxbus", O_RDWR);
	if (audio_fd < 0) {
		fail("open /dev/rtxbus");
		goto out;
	}
	if (ioctl(audio_fd, RTXBUS_RESET, 0) != 0) {
		fail("RTXBUS_RESET");
		goto out;
	}
	if (ioctl(audio_fd, RTXBUS_COMMAND, &init) < 0 || init.status != 0) {
		if (errno == 0)
			errno = EIO;
		fail("AC init");
		goto out;
	}
	if (init.map_length == 0 || init.tail_length > init.map_length) {
		fprintf(stderr, "AC init returned an invalid AO shared-memory tail\n");
		goto out;
	}
	shared = mmap(NULL, init.map_length, PROT_READ | PROT_WRITE,
		MAP_SHARED, audio_fd, (off_t)init.map_offset);
	if (shared == MAP_FAILED) {
		fail("AC shared-memory mmap");
		goto out;
	}
	if (ac_set_init_params(audio_fd) < 0) {
		fail("AC set init params");
		goto out;
	}
	/* AO type 3, 16-bit mono PCM; remaining values come from the profile. */
	if (ac_set_retail_config(audio_fd, 3, sample_rate, packet_bytes,
		volume) < 0) {
		fail("AC set retail AO config");
		goto out;
	}
	if (ac_simple(audio_fd, AC_CMD_AO_ENABLE, 0) < 0) {
		fail("AC AO enable");
		goto out;
	}
	ao_enabled = 1;

	/* Prime one silent 20 ms packet while the external amplifier is muted. */
	memset(packet, 0, packet_bytes);
	if (ac_send_ao_frame(audio_fd, shared, &init, packet,
			     packet_bytes) < 0) {
		fail("AC AO silent frame");
		goto out;
	}
	usleep(frame_us);
	if (speaker_set_mute(0) < 0) {
		fail("release GPIO24 speaker mute");
		goto out;
	}
	if (pcm_path != NULL) {
		pcm_fd = pcm_stream ? dup(STDIN_FILENO) : open(pcm_path, O_RDONLY);
		if (pcm_fd < 0) {
			fail("open PCM input");
			goto out;
		}
		while (!playback_stop && (pcm_stream || played_frames < frame_limit)) {
			pcm_bytes = read(pcm_fd, packet, packet_bytes);
			if (pcm_bytes < 0) {
				fail("read PCM input");
				goto out;
			}
			if (pcm_bytes == 0)
				break;
			if ((size_t)pcm_bytes < packet_bytes)
				memset((uint8_t *)packet + pcm_bytes, 0,
				       packet_bytes - (size_t)pcm_bytes);
			if (ac_send_ao_frame(audio_fd, shared, &init, packet,
					     packet_bytes) < 0) {
				fail("AC AO PCM frame");
				goto out;
			}
			played_frames++;
			usleep(frame_us);
		}
		if (!pcm_stream && played_frames == frame_limit) {
			fprintf(stderr, "PCM playback stopped at 60-second safety limit\n");
			goto out;
		}
	} else {
		/* Short Mario opening phrase, 64-step sine, peak 4096 (12.5%). */
		for (note_index = 0;
		     note_index < sizeof(melody) / sizeof(melody[0]) && !playback_stop;
		     ++note_index) {
			phase = 0;
			phase_step = melody[note_index].frequency == 0 ? 0 :
				(uint32_t)(((uint64_t)melody[note_index].frequency << 32) /
					   sample_rate);
			for (note_frame = 0;
			     note_frame < (unsigned)melody[note_index].frames * 2U &&
			     !playback_stop;
			     ++note_frame) {
				for (sample_index = 0; sample_index < packet_samples;
				     ++sample_index) {
					if (melody[note_index].frequency == 0) {
						packet[sample_index] = 0;
						continue;
					}
					packet[sample_index] =
						(int16_t)(sine_q15[phase >> 26] / 8);
					phase += phase_step;
				}
				if (ac_send_ao_frame(audio_fd, shared, &init, packet,
						     packet_bytes) < 0) {
					fail("AC AO melody frame");
					goto out;
				}
				played_frames++;
				usleep(frame_us);
			}
		}
	}
	if (playback_stop) {
		errno = EINTR;
		fail("speaker test interrupted");
		goto out;
	}
	rc = 0;

out:
	/* Safety invariant: mute before stopping AO or releasing any resource. */
	if (speaker_mute_fd >= 0 && speaker_set_mute(1) < 0 && rc == 0)
		rc = fail("restore GPIO24 speaker mute");
	if (ao_enabled && ac_simple(audio_fd, AC_CMD_AO_DISABLE, 0) < 0 && rc == 0)
		rc = fail("AC AO disable");
	if (shared != MAP_FAILED)
		munmap(shared, init.map_length);
	if (audio_fd >= 0)
		close(audio_fd);
	if (pcm_fd >= 0)
		close(pcm_fd);
	if (speaker_mute_fd >= 0)
		close(speaker_mute_fd);
	speaker_mute_fd = -1;
	if (rc == 0) {
		printf("played=%u transport=rtxbus rate=%u format=s16le "
		       "pcm_bitrate=%u packet=%u volume=%u source=%s duration_ms=%u "
		       "mute_gpio=%u mute_active=%u restored=1\n",
		       played_frames * packet_samples, sample_rate,
		       sample_rate * 16U, packet_bytes, volume,
		       pcm_stream ? "stdin" :
		       (pcm_path != NULL ? pcm_path : "mario-intro-sine64"),
		       (unsigned)(((uint64_t)played_frames * frame_us) / 1000U),
		       speaker_mute_gpio, speaker_mute_active);
	}
	return rc;
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "play") == 0)
		return playback_test(NULL);
	if (argc > 1 && strcmp(argv[1], "play-file") == 0) {
		if (argc != 3) {
			fprintf(stderr, "usage: %s play-file PCM_S16LE_16K_MONO\n",
				argv[0]);
			return 2;
		}
		return playback_test(argv[2]);
	}

	int streaming = argc > 1 && strcmp(argv[1], "stream") == 0;
	const char *path = streaming ? "stdout" :
		(argc > 1 ? argv[1] : "/tmp/fh-mic-s16le.raw");
	long wanted = streaming ? LONG_MAX :
		(argc > 2 ? strtol(argv[2], NULL, 0) : 64000);
	struct ac_init_command init = {
		.size = 0x18,
		.size_a = 0x18,
		.size_b = 0x18,
		.opcode = 0x01040004,
	};
	uint64_t sum_abs = 0;
	uint64_t samples = 0;
	unsigned sample_rate = env_uint("FH8626_AUDIO_CAPTURE_RATE", 8000,
		8000, 48000);
	unsigned packet_bytes = env_uint("FH8626_AUDIO_CAPTURE_PACKET", 320,
		32, 2048);
	unsigned volume = env_uint("FH8626_AUDIO_CAPTURE_VOLUME", 31, 0, 31);
	unsigned nr_enabled = env_uint("FH8626_AUDIO_CAPTURE_NR", 1, 0, 1);
	unsigned nr_level = env_uint("FH8626_AUDIO_CAPTURE_NR_LEVEL", 3, 0, 31);
	int16_t min_sample = INT16_MAX;
	int16_t max_sample = INT16_MIN;
	struct timespec started;
	long total = 0;
	uint8_t *shared = MAP_FAILED;
	int audio_fd = -1;
	int out_fd = -1;
	int enabled = 0;
	int rc = 1;

	if (!supported_rate(sample_rate)) {
		fprintf(stderr, "unsupported capture rate %u\n", sample_rate);
		return 2;
	}
	if ((packet_bytes & 3U) != 0) {
		fprintf(stderr, "capture packet must be divisible by 4\n");
		return 2;
	}

	if (wanted <= 0 || wanted > 1048576) {
		if (streaming)
			goto size_ok;
		fprintf(stderr, "capture size must be 1..1048576 bytes\n");
		return 2;
	}
size_ok:
	capture_stop = 0;
	signal(SIGINT, capture_signal);
	signal(SIGTERM, capture_signal);

	audio_fd = open("/dev/rtxbus", O_RDWR);
	if (audio_fd < 0)
		return fail("open /dev/rtxbus");
	if (ioctl(audio_fd, RTXBUS_RESET, 0) != 0) {
		fail("RTXBUS_RESET");
		goto out;
	}
	if (ioctl(audio_fd, RTXBUS_COMMAND, &init) < 0 || init.status != 0) {
		if (errno == 0)
			errno = EIO;
		fail("AC init");
		goto out;
	}
	if (init.map_length == 0) {
		fprintf(stderr, "AC init returned an empty shared-memory map\n");
		goto out;
	}
	shared = mmap(NULL, init.map_length, PROT_READ | PROT_WRITE,
		MAP_SHARED, audio_fd, (off_t)init.map_offset);
	if (shared == MAP_FAILED) {
		fail("AC shared-memory mmap");
		goto out;
	}
	/* Apollo's exact board startup order; the DSP needs both before AI enable. */
	if (ac_set_init_params(audio_fd) < 0) {
		fail("AC set init params");
		goto out;
	}
	if (ac_set_capture_nr(audio_fd, nr_enabled, nr_level) < 0) {
		fail("AC set capture NR");
		goto out;
	}
	if (ac_set_retail_config(audio_fd, 0, sample_rate, packet_bytes,
		volume) < 0) {
		fail("AC set retail AI config");
		goto out;
	}
	if (ac_simple(audio_fd, AC_CMD_AI_ENABLE, 0) < 0) {
		fail("AC AI enable");
		goto out;
	}
	enabled = 1;
	if (ac_simple(audio_fd, AC_CMD_AI_VOLUME, volume) < 0) {
		fail("AC AI volume");
		goto out;
	}

	out_fd = streaming ? dup(STDOUT_FILENO) :
		open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out_fd < 0) {
		fail("open output");
		goto out;
	}
	clock_gettime(CLOCK_MONOTONIC, &started);

	while (!capture_stop && total < wanted) {
		struct ac_frame_command frame = {
			.size = 0x18,
			.size_a = 0x18,
			.size_b = 8,
			.opcode = 0x01008000,
		};
		struct timespec now;
		uint32_t relative;
		uint32_t count;
		uint32_t i;

		if (ioctl(audio_fd, RTXBUS_COMMAND, &frame) < 0) {
			fail("AC get frame");
			goto out;
		}
		/* Stock polls until the RTX audio service publishes the next frame. */
		if (frame.status != 0 || frame.data_length == 0) {
			clock_gettime(CLOCK_MONOTONIC, &now);
			if (elapsed_ms(&started, &now) > 10000) {
				fprintf(stderr,
					"AC capture timed out: status=0x%08x length=%u\n",
					(uint32_t)frame.status, frame.data_length);
				goto out;
			}
			usleep(10000);
			continue;
		}
		if (frame.data_offset < init.map_offset) {
			fprintf(stderr, "AC frame offset precedes shared map\n");
			goto out;
		}
		relative = frame.data_offset - init.map_offset;
		if (relative > init.map_length ||
			frame.data_length > init.map_length - relative) {
			fprintf(stderr, "AC frame lies outside shared map\n");
			goto out;
		}
		count = frame.data_length;
		if (!streaming && count > (uint32_t)(wanted - total))
			count = (uint32_t)(wanted - total);
		if (write(out_fd, shared + relative, count) != (ssize_t)count) {
			fail("write output");
			goto out;
		}
		for (i = 0; i + 1 < count; i += 2) {
			int16_t sample = (int16_t)((uint16_t)shared[relative + i] |
				((uint16_t)shared[relative + i + 1] << 8));
			int32_t magnitude = sample;

			if (sample < min_sample)
				min_sample = sample;
			if (sample > max_sample)
				max_sample = sample;
			if (magnitude < 0)
				magnitude = -magnitude;
			sum_abs += (uint32_t)magnitude;
			samples++;
		}
		total += count;
	}
	rc = 0;

out:
	if (enabled && ac_simple(audio_fd, AC_CMD_AI_DISABLE, 0) < 0 && rc == 0)
		rc = fail("AC AI disable");
	if (out_fd >= 0)
		close(out_fd);
	if (shared != MAP_FAILED)
		munmap(shared, init.map_length);
	if (audio_fd >= 0)
		close(audio_fd);

	if (rc == 0 && !streaming) {
		printf("captured=%ld path=%s transport=rtxbus rate=%u "
		       "format=s16le packet=%u volume=%u nr=%u nr_level=%u "
		       "min=%d max=%d mean_abs=%llu\n",
		       total, path, sample_rate, packet_bytes, volume,
		       nr_enabled, nr_level, min_sample, max_sample,
		       (unsigned long long)(samples ? sum_abs / samples : 0));
	} else if (rc == 0) {
		fprintf(stderr, "stream_stopped bytes=%ld transport=rtxbus rate=%u "
			"format=s16le packet=%u volume=%u nr=%u nr_level=%u\n",
			total, sample_rate, packet_bytes, volume, nr_enabled,
			nr_level);
	}
	return rc;
}
