/*
 * (C) 2017 Ginzinger electronic systems GmbH, A-4952 Weng im Innkreis
 *
 * Martin Kepplinger <martin.kepplinger@ginzinger.com>  2016-09-14
 * Melchior FRANZ <melchior.franz@ginzinger.com>  2015-09-30
 *
 * This file is part of tslib.
 *
 * ts_uinput is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * ts_uinput is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ts_uinput.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * ts_uinput daemon to generate (single- and multitouch) input events
 * taken from tslib multitouch samples. It's a userspace evdev driver
 * and thus Linux specific.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <tslib.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <dirent.h>
#ifdef __FreeBSD__
#include <dev/evdev/input.h>
#include <dev/evdev/uinput.h>
#include <sys/types.h>
#include <sys/fbio.h>
#else
#include <linux/input.h>
#include <linux/uinput.h>
#include <linux/fb.h>
#endif

#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define BLUE    "\033[34m"

#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)	((array[LONG(bit)] >> OFF(bit)) & 1)

#define DEFAULT_UINPUT_NAME "ts_uinput"

#ifndef ABS_MT_SLOT /* < 2.6.36 kernel headers */
# define ABS_MT_SLOT             0x2f    /* MT slot being modified */
#endif
#ifndef ABS_MT_POSITION_X /* < 2.6.30 kernel headers */
# define ABS_MT_TOUCH_MAJOR      0x30    /* Major axis of touching ellipse */
# define ABS_MT_TOUCH_MINOR      0x31    /* Minor axis (omit if circular) */
# define ABS_MT_WIDTH_MAJOR      0x32    /* Major axis of approaching ellipse */
# define ABS_MT_WIDTH_MINOR      0x33    /* Minor axis (omit if circular) */
# define ABS_MT_ORIENTATION      0x34    /* Ellipse orientation */
# define ABS_MT_POSITION_X       0x35    /* Center X touch position */
# define ABS_MT_POSITION_Y       0x36    /* Center Y touch position */
# define ABS_MT_TOOL_TYPE        0x37    /* Type of touching device */
# define ABS_MT_BLOB_ID          0x38    /* Group a set of packets as a blob */
# define ABS_MT_TRACKING_ID      0x39    /* Unique ID of initiated contact */
#endif
#ifndef ABS_MT_PRESSURE /* < 2.6.33 kernel headers */
# define ABS_MT_PRESSURE         0x3a    /* Pressure on contact area */
#endif
#ifndef ABS_MT_DISTANCE /* < 2.6.38 kernel headers */
# define ABS_MT_DISTANCE         0x3b    /* Contact hover distance */
#endif
#ifndef ABS_MT_TOOL_X /* < 3.6 kernel headers */
# define ABS_MT_TOOL_X           0x3c    /* Center X tool position */
# define ABS_MT_TOOL_Y           0x3d    /* Center Y tool position */
#endif

#define SYS_INPUT_DIR "/sys/devices/virtual/input/"

#ifndef UINPUT_VERSION
#define UINPUT_VERSION	2
#endif

struct data_t {
	int fd_uinput;
	int fd_input;
	int fd_fb;
	char *uinput_name;
	char *input_name;
	char *fb_name;
	struct tsdev *ts;
	unsigned short verbose;
	unsigned short verbose_daemon;
	struct input_event *ev;
	struct ts_sample_mt **s_array;
	int slots;
	unsigned short uinput_version;
	short mt_type_a;
};

static void help(void)
{
	struct ts_lib_version_data *ver = ts_libversion();

	printf("tslib %s (library 0x%X)\n", ver->package_version, ver->version_num);
	printf("\n");
	printf("Starts tslib instance listening to given event <device>, creates a virtual\n");
	printf("input event device with given <name> using 'uinput', then continually reads\n");
	printf("touch reports from tslib and replays them as touch events of protocol type B\n");
	printf("on the virtual device.\n");
	printf("\n");
	printf("Usage: ts_uinput [-v] [-d] [-i <device>] [-f <device>] [-n <name>] [-s <slots>]\n");
	printf("\n");
	printf("  -h, --help          this help text\n");
	printf("  -d, --daemonize     run in the background as a daemon\n");
	printf("  -v, --verbose       verbose output\n");
	printf("  -n, --name          set name of new input device  (default: " DEFAULT_UINPUT_NAME")\n");
	printf("  -i, --idev          touchscreen's input device\n");
	printf("  -f, --fbdev         touchscreen's framebuffer device\n");
	printf("  -s, --slots         override available concurrent touch contacts\n");
	printf("\n");
	printf("See the manpage for further details.\n");
}

#define MAX_CODES_PER_SLOT 20
static int send_touch_events(struct data_t *data, struct ts_sample_mt **s,
			     int nr, int max_slots)
{
	int i;
	int j;
	int c = 0;

	for (j = 0; j < nr; j++) {
		memset(data->ev,
		       0,
		       sizeof(struct input_event) * MAX_CODES_PER_SLOT * max_slots);

		for (i = 0; i < max_slots; i++) {
			if (s[j][i].pen_down == 1 || s[j][i].pen_down == 0) {
				data->ev[c].time = s[j][i].tv;
				data->ev[c].type = EV_KEY;
				data->ev[c].code = BTN_TOUCH;
				data->ev[c].value = s[j][i].pen_down;
				c++;
			}

			if (s[j][i].valid != 1)
				continue;

			data->ev[c].time = s[j][i].tv;
			data->ev[c].type = EV_ABS;
			data->ev[c].code = ABS_MT_SLOT;
			data->ev[c].value = s[j][i].slot;
			c++;

			/*
			 * This simply supports legacy input events when only
			 * one finger is used.
			 * XXX We should track slot 0, and if it is gone
			 * we should use slot 1 and so on.
			 */
			if (i == 0) {
				data->ev[c].time = s[j][i].tv;
				data->ev[c].type = EV_ABS;
				data->ev[c].code = ABS_X;
				data->ev[c].value = s[j][i].x;
				c++;

				data->ev[c].time = s[j][i].tv;
				data->ev[c].type = EV_ABS;
				data->ev[c].code = ABS_Y;
				data->ev[c].value = s[j][i].y;
				c++;

				data->ev[c].time = s[j][i].tv;
				data->ev[c].type = EV_ABS;
				data->ev[c].code = ABS_PRESSURE;
				data->ev[c].value = s[j][i].pressure;
				c++;
			}

			data->ev[c].time = s[j][i].tv;
			data->ev[c].type = EV_ABS;
			data->ev[c].code = ABS_MT_POSITION_X;
			data->ev[c].value = s[j][i].x;
			c++;

			data->ev[c].time = s[j][i].tv;
			data->ev[c].type = EV_ABS;
			data->ev[c].code = ABS_MT_POSITION_Y;
			data->ev[c].value = s[j][i].y;
			c++;

			data->ev[c].time = s[j][i].tv;
			data->ev[c].type = EV_ABS;
			data->ev[c].code = ABS_MT_PRESSURE;
			data->ev[c].value = s[j][i].pressure;
			c++;

			data->ev[c].time = s[j][i].tv;
			data->ev[c].type = EV_ABS;
			data->ev[c].code = ABS_MT_TOUCH_MAJOR;
			data->ev[c].value = s[j][i].touch_major;
			c++;

			data->ev[c].time = s[j][i].tv;
			data->ev[c].type = EV_ABS;
			data->ev[c].code = ABS_MT_WIDTH_MAJOR;
			data->ev[c].value = s[j][i].width_major;
			c++;

			data->ev[c].time = s[j][i].tv;
			data->ev[c].type = EV_ABS;
			data->ev[c].code = ABS_MT_TOUCH_MINOR;
			data->ev[c].value = s[j][i].touch_minor;
			c++;

			data->ev[c].time = s[j][i].tv;
			data->ev[c].type = EV_ABS;
			data->ev[c].code = ABS_MT_WIDTH_MINOR;
			data->ev[c].value = s[j][i].width_minor;
			c++;

			data->ev[c].time = s[j][i].tv;
			data->ev[c].type = EV_ABS;
			data->ev[c].code = ABS_MT_TOOL_TYPE;
			data->ev[c].value = s[j][i].tool_type;
			c++;

			data->ev[c].time = s[j][i].tv;
			data->ev[c].type = EV_ABS;
			data->ev[c].code = ABS_MT_TOOL_X;
			data->ev[c].value = s[j][i].tool_x;
			c++;

			data->ev[c].time = s[j][i].tv;
			data->ev[c].type = EV_ABS;
			data->ev[c].code = ABS_MT_TOOL_Y;
			data->ev[c].value = s[j][i].tool_y;
			c++;

			data->ev[c].time = s[j][i].tv;
			data->ev[c].type = EV_ABS;
			data->ev[c].code = ABS_MT_ORIENTATION;
			data->ev[c].value = s[j][i].orientation;
			c++;

			data->ev[c].time = s[j][i].tv;
			data->ev[c].type = EV_ABS;
			data->ev[c].code = ABS_MT_DISTANCE;
			data->ev[c].value = s[j][i].distance;
			c++;

			data->ev[c].time = s[j][i].tv;
			data->ev[c].type = EV_ABS;
			data->ev[c].code = ABS_MT_BLOB_ID;
			data->ev[c].value = s[j][i].blob_id;
			c++;

			data->ev[c].time = s[j][i].tv;
			data->ev[c].type = EV_ABS;
			data->ev[c].code = ABS_MT_TRACKING_ID;
			data->ev[c].value = s[j][i].tracking_id;
			c++;

			if (data->mt_type_a == 1) {
				data->ev[c].time = s[j][i].tv;
				data->ev[c].type = EV_SYN;
				data->ev[c].code = SYN_MT_REPORT;
				data->ev[c].value = 0;
				c++;
			}
		}

		if (c > 0) {
			data->ev[c].time = s[j][i].tv;
			data->ev[c].type = EV_SYN;
			data->ev[c].code = SYN_REPORT;
			data->ev[c].value = 0;


			if (write(data->fd_uinput,
				  data->ev,
				  sizeof(struct input_event) * (c + 1)) < 0) {
				perror("write");
				return errno;
			}
		}

		c = 0;
	}

	return 0;
}

static int setup_uinput(struct data_t *data, int *max_slots)
{
	struct uinput_user_dev uidev;
	unsigned long bit[EV_MAX][NBITS(KEY_MAX)];
	int i, j;
	struct input_absinfo absinfo;
	struct fb_var_screeninfo fbinfo;

	if (ioctl(data->fd_fb, FBIOGET_VSCREENINFO, &fbinfo) < 0) {
		perror("ioctl FBIOGET_VSCREENINFO");
		goto err;
	}

	data->fd_uinput = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (data->fd_uinput == -1) {
		perror("open /dev/uinput");
		goto err;
	}

	if (ioctl(data->fd_uinput, UI_SET_EVBIT, EV_KEY) < 0 ||
	    ioctl(data->fd_uinput, UI_SET_EVBIT, EV_SYN) < 0 ||
	    ioctl(data->fd_uinput, UI_SET_KEYBIT, BTN_TOUCH) < 0 ||
	    ioctl(data->fd_uinput, UI_SET_EVBIT, EV_ABS) < 0) {
		perror("ioctl");
		goto err;
	}

	memset(&uidev, 0, sizeof(uidev));
	snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "%s", data->uinput_name);
	uidev.id.bustype = BUS_VIRTUAL;

	memset(bit, 0, sizeof(bit));

	/* get info on input device and copy setting over to uinput device */
	if (ioctl(data->fd_input, EVIOCGBIT(0, EV_MAX), bit[0]) < 0) {
		perror("ioctl EVIOCGBIT");
		goto err;
	}
	for (i = 0; i < EV_MAX; i++) {
		if (test_bit(i, bit[0])) {
			if (ioctl(data->fd_input,
				  EVIOCGBIT(i, KEY_MAX),
				  bit[i]) < 0) {
				perror("ioctl EVIOCGBIT");
				goto err;
			}

			for (j = 0; j < KEY_MAX; j++) {
				if (test_bit(j, bit[i])) {
					if (i == EV_ABS) {
						if (ioctl(data->fd_input,
							  EVIOCGABS(j),
							  &absinfo) < 0) {
							perror("ioctl EVIOCGABS");
							goto err;
						}

						if (ioctl(data->fd_uinput,
							  UI_SET_ABSBIT,
							  j) < 0) {
							perror("ioctl UI_SET_ABSBIT");
							goto err;
						}

						/*
						 * X and Y max/min are taken from the framebuffer device
						 * The rest comes from the input device.
						 */

						if (j == ABS_X) {
							uidev.absmin[ABS_X] = 0;
							uidev.absmax[ABS_X] = fbinfo.xres - 1;
						} else if (j == ABS_Y) {
							uidev.absmin[ABS_Y] = 0;
							uidev.absmax[ABS_Y] = fbinfo.yres - 1;
						} else if (j == ABS_MT_POSITION_X) {
							uidev.absmin[ABS_MT_POSITION_X] = 0;
							uidev.absmax[ABS_MT_POSITION_X] = fbinfo.xres - 1;
						} else if (j == ABS_MT_POSITION_Y) {
							uidev.absmin[ABS_MT_POSITION_Y] = 0;
							uidev.absmax[ABS_MT_POSITION_Y] = fbinfo.yres - 1;
						} else {
							uidev.absmin[j] = absinfo.minimum;
							uidev.absmax[j] = absinfo.maximum;
						}

						if (j == ABS_MT_SLOT) {
							*max_slots = absinfo.maximum + 1 - absinfo.minimum;
						}
					} else if (i == EV_SYN) {
						if (j == SYN_MT_REPORT)
							data->mt_type_a = 1;
					}
				}
			}
		}
	}

	if (write(data->fd_uinput, &uidev, sizeof(uidev)) == -1) {
		perror("write uinput_user_dev");
		goto err;
	}

	if (ioctl(data->fd_uinput, UI_DEV_CREATE) < 0) {
		perror("ioctl UI_DEV_CREATE");
		goto err;
	}

	return 0;

err:
	return errno;
}

static int process(struct data_t *data, struct ts_sample_mt **s_array,
		   int max_slots, int nr)
{
	int samples_read;
	int i, j;
	int ret;

	samples_read = ts_read_mt(data->ts, s_array, max_slots, nr);
	if (samples_read > 0) {
		ret = send_touch_events(data, s_array, samples_read, max_slots);
		if (ret)
			return ret;

		if (data->verbose) {
			for (j = 0; j < nr; j++) {
				printf(BLUE DEFAULT_UINPUT_NAME
				       ": sample %d:  x\ty\tslot\ttracking_id\n"
				       RESET, j);
				for (i = 0; i < max_slots; i++) {
					if (s_array[j][i].valid == 1) {
						printf(DEFAULT_UINPUT_NAME
						       ": \t%d\t%d\t%d\t%d\n",
						       s_array[j][i].x,
						       s_array[j][i].y,
						       s_array[j][i].slot,
						       s_array[j][i].tracking_id);
					}
				}
			}
		}
	} else if (samples_read < 0 && samples_read != -EAGAIN) {
		if (data->verbose)
			fprintf(stderr, RED DEFAULT_UINPUT_NAME
				": ts_read_mt failure.\n" RESET);

		return samples_read;
	}

	return 0;
}

#define TS_READ_WHOLE_SAMPLES 1

static void cleanup(struct data_t *data)
{
	int i;
	int ret;

	if (data->s_array) {
		for (i = 0; i < TS_READ_WHOLE_SAMPLES; i++) {
			if (data->s_array[i])
				free(data->s_array[i]);
		}
		free(data->s_array);
	}

	if (data->ev)
		free(data->ev);

	if (data->ts)
		ts_close(data->ts);

	if (data->fd_uinput > 0) {
		ret = ioctl(data->fd_uinput, UI_DEV_DESTROY);
		if (ret == -1)
			perror("ioctl UI_DEV_DESTROY");

		close(data->fd_uinput);
	}

	if (data->fd_input)
		close(data->fd_input);

	if (data->fd_fb)
		close(data->fd_fb);

	if (data->uinput_name)
		free(data->uinput_name);
}

/* directly from libevdev (LGPL) */
static int is_event_device(const struct dirent *dent) {
	return strncmp("event", dent->d_name, 5) == 0;
}

/* directly from libevdev (LGPL) */
static char *fetch_device_node(const char *path)
{
	char *devnode = NULL;
	struct dirent **namelist;
	int ndev, i;

	ndev = scandir(path, &namelist, is_event_device, alphasort);
	if (ndev <= 0)
		return NULL;

	/* ndev should only ever be 1 */

	for (i = 0; i < ndev; i++) {
		if (!devnode && asprintf(&devnode, "/dev/input/%s", namelist[i]->d_name) == -1)
			devnode = NULL;
		free(namelist[i]);
	}

	free(namelist);

	return devnode;
}

int main(int argc, char **argv)
{
	struct data_t data = {
		.fd_uinput = 0,
		.fd_input = 0,
		.uinput_name = NULL,
		.input_name = NULL,
		.fb_name = NULL,
		.ts = NULL,
		.verbose = 0,
		.ev = NULL,
		.s_array = NULL,
		.slots = 1,
		.mt_type_a = 0,
		.verbose_daemon = 0,
	};
	int i, j;
	unsigned short run_daemon = 0;

	while (1) {
		const struct option long_options[] = {
			{ "help",         no_argument,       NULL, 'h' },
			{ "name",         required_argument, NULL, 'n' },
			{ "verbose",      no_argument,       NULL, 'v' },
			{ "daemonize",    no_argument,       NULL, 'd' },
			{ "idev",         required_argument, NULL, 'i' },
			{ "fbdev",        required_argument, NULL, 'f' },
			{ "slots",        required_argument, NULL, 's' },
		};

		int option_index = 0;
		int c = getopt_long(argc, argv, "dhn:f:i:vs:", long_options,
				    &option_index);

		if (c == -1)
			break;

		errno = 0;
		switch (c) {
		case 'h':
			help();
			return 0;

		case 'n':
			data.uinput_name = optarg;
			break;

		case 'v':
			data.verbose = 1;
			break;

		case 'd':
			run_daemon = 1;
			break;

		case 'i':
			data.input_name = optarg;
			break;

		case 'f':
			data.fb_name = optarg;
			break;

		case 's':
			data.slots = atoi(optarg);
			break;

		default:
			help();
			return 0;
		}

		if (errno) {
			char *str = "option ?";

			str[7] = c & 0xff;
			perror(str);
		}
	}

	/* if we run as a daemon, we don't print all debug output. we print
	 * the input device node before returning. */
	if (data.verbose && run_daemon) {
		data.verbose = 0;
		data.verbose_daemon = 1;
	}

	if (data.verbose) {
		printf(BLUE "\ntslib environment variables:" RESET "\n");
		printf("       TSLIB_TSDEVICE: '%s'\n", getenv("TSLIB_TSDEVICE"));
		printf("      TSLIB_PLUGINDIR: '%s'\n", getenv("TSLIB_PLUGINDIR"));
		printf("  TSLIB_CONSOLEDEVICE: '%s'\n", getenv("TSLIB_CONSOLEDEVICE"));
		printf("       TSLIB_FBDEVICE: '%s'\n", getenv("TSLIB_FBDEVICE"));
		printf("      TSLIB_CALIBFILE: '%s'\n", getenv("TSLIB_CALIBFILE"));
		printf("       TSLIB_CONFFILE: '%s'\n", getenv("TSLIB_CONFFILE"));
		printf("\n");
	}

	if (!data.uinput_name) {
		data.uinput_name = malloc(strlen(DEFAULT_UINPUT_NAME) + 1);
		if (!data.uinput_name)
			return errno;
		sprintf(data.uinput_name, DEFAULT_UINPUT_NAME);
	}

	if (!data.fb_name) {
		if (getenv("TSLIB_FBDEVICE")) {
			data.fb_name = getenv("TSLIB_FBDEVICE");
		} else {
			fprintf(stderr, RED DEFAULT_UINPUT_NAME
				": no framebuffer device specified"
				RESET "\n");
			goto out;
		}
	}

	data.fd_fb = open(data.fb_name, O_RDWR);
	if (data.fd_fb == -1) {
		perror("open");
		goto out;
	}

	if (data.verbose)
		printf(DEFAULT_UINPUT_NAME ": using framebuffer device "
		       GREEN "%s" RESET "\n",
		       getenv("TSLIB_FBDEVICE"));

	if (!data.input_name) {
		if (getenv("TSLIB_TSDEVICE")) {
			data.input_name = getenv("TSLIB_TSDEVICE");
		} else {
			fprintf(stderr, RED DEFAULT_UINPUT_NAME
				": no input device specified" RESET "\n");
			goto out;
		}
	}

	data.fd_input = open(data.input_name, O_RDWR);
	if (data.fd_input == -1) {
		perror("open");
		goto out;
	}

	if (data.verbose)
		printf(DEFAULT_UINPUT_NAME
		       ": using input device " GREEN "%s" RESET "\n",
		       data.input_name);

	if (setup_uinput(&data, &data.slots)) {
		goto out;
	} else {
		if (data.verbose && data.slots == 1)
			printf(DEFAULT_UINPUT_NAME
			       ": We don't use a multitouch device\n");
		else if (data.verbose && data.slots > 1)
			printf(DEFAULT_UINPUT_NAME
			       ": We use a "
			       GREEN "multitouch" RESET
			       " device\n");
	}

	if (data.verbose) {
		printf(DEFAULT_UINPUT_NAME ": running uinput version %d\n", UINPUT_VERSION);
		#if UINPUT_VERSION >= 4
			char name[64];
			int ret = ioctl(data.fd_uinput,
					UI_GET_SYSNAME(sizeof(name)),
					name);
			if (ret >= 0) {
				char buf[sizeof(SYS_INPUT_DIR) + sizeof(name)] = SYS_INPUT_DIR;
				char *devnode;

				snprintf(&buf[strlen(SYS_INPUT_DIR)], sizeof(name), "%s", name);
				fprintf(stdout, "created %s\n", buf);
				devnode = fetch_device_node(buf);
				if (devnode)
					fprintf(stdout, "%s\n", devnode);
			}
		#else
			fprintf(stderr, DEFAULT_UINPUT_NAME
				": See the kernel log for the device number\n");
		#endif
	}

	data.ev = malloc(sizeof(struct input_event) * MAX_CODES_PER_SLOT * data.slots);
	if (!data.ev)
		goto out;

	data.s_array = calloc(TS_READ_WHOLE_SAMPLES,
			      sizeof(struct ts_sample_mt *));
	if (!data.s_array)
		goto out;

	for (i = 0; i < TS_READ_WHOLE_SAMPLES; i++) {
		data.s_array[i] = malloc(data.slots * sizeof(struct ts_sample_mt));
		if (!data.s_array[i]) {
			fprintf(stderr, DEFAULT_UINPUT_NAME
				": Error allocating memory\n");
			for (j = 0; j <= i; j++)
				free(data.s_array[j]);
			goto out;
		}
	}

	/* non-blocking for one test run in order to verify reading and fail before forking */
	data.ts = ts_open(data.input_name, 1);
	if (!data.ts) {
		perror("ts_open");
		goto out;
	}

	if (ts_config(data.ts)) {
		perror("ts_config");
		goto out;
	}

	if (process(&data, data.s_array, data.slots, TS_READ_WHOLE_SAMPLES))
		goto out;

	ts_close(data.ts);

	/* blocking setup for production run */
	data.ts = ts_open(data.input_name, 0);
	if (!data.ts) {
		perror("ts_open");
		goto out;
	}

	if (ts_config(data.ts)) {
		perror("ts_config");
		goto out;
	}

	if (run_daemon) {
		#if UINPUT_VERSION >= 4
			char name[64];
			int ret = ioctl(data.fd_uinput,
					UI_GET_SYSNAME(sizeof(name)),
					name);
			if (ret >= 0) {
				if (data.verbose_daemon) {
					char buf[sizeof(SYS_INPUT_DIR) + sizeof(name)] = SYS_INPUT_DIR;
					char *devnode;

					snprintf(&buf[strlen(SYS_INPUT_DIR)], sizeof(name), "%s", name);
					devnode = fetch_device_node(buf);
					if (devnode)
						fprintf(stdout, "%s\n", devnode);
				} else {
					fprintf(stdout, "%s\n", name);
				}

				fflush(stdout);
			} else {
				perror("ioctl UI_GET_SYSNAME");
				goto out;
			}
		#else
			fprintf(stderr, DEFAULT_UINPUT_NAME
			": See the kernel log for the device number\n");
		#endif
		if (daemon(0, 0) == -1) {
			perror("error starting daemon");
			goto out;
		}
	}

	while (1) {
		if (process(&data, data.s_array, data.slots,
			    TS_READ_WHOLE_SAMPLES))
			goto out;
	}

out:
	cleanup(&data);

	return errno;
}
