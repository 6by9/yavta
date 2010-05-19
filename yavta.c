/*
 * yavta --  Yet Another V4L2 Test Application
 *
 * Copyright (C) 2005-2010 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>

#include <linux/videodev2.h>

#define ARRAY_SIZE(a)	(sizeof(a)/sizeof((a)[0]))

struct device
{
	int fd;

	enum v4l2_buf_type type;
	enum v4l2_memory memtype;
	unsigned int nbufs;
	unsigned int bufsize;
	void **mem;
};

static const char *v4l2_buf_type_name(enum v4l2_buf_type type)
{
	static struct {
		enum v4l2_buf_type type;
		const char *name;
	} names[] = {
		{ V4L2_BUF_TYPE_VIDEO_CAPTURE, "Video capture" },
		{ V4L2_BUF_TYPE_VIDEO_OUTPUT, "Video output" },
		{ V4L2_BUF_TYPE_VIDEO_OVERLAY, "Video overlay" },
	};

	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(names); ++i) {
		if (names[i].type == type)
			return names[i].name;
	}

	if (type & V4L2_BUF_TYPE_PRIVATE)
		return "Private";
	else
		return "Unknown";
}

static const char *v4l2_fourcc_name(unsigned int fourcc)
{
	static char name[5];
	unsigned int i;

	for (i = 0; i < 4; ++i) {
		name[i] = fourcc & 0xff;
		fourcc >>= 8;
	}

	name[4] = '\0';
	return name;
}

static int video_open(struct device *dev, const char *devname, int no_query)
{
	struct v4l2_capability cap;
	int ret;

	memset(dev, 0, sizeof *dev);
	dev->fd = -1;
	dev->memtype = V4L2_MEMORY_MMAP;
	dev->mem = NULL;

	dev->fd = open(devname, O_RDWR);
	if (dev->fd < 0) {
		printf("Error opening device %s: %d.\n", devname, errno);
		return dev->fd;
	}

	if (!no_query) {
		memset(&cap, 0, sizeof cap);
		ret = ioctl(dev->fd, VIDIOC_QUERYCAP, &cap);
		if (ret < 0) {
			printf("Error opening device %s: unable to query "
				"device.\n", devname);
			close(dev->fd);
			return ret;
		}

		if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)
			dev->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		else if (cap.capabilities & V4L2_CAP_VIDEO_OUTPUT)
			dev->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		else {
			printf("Error opening device %s: neither video capture "
				"nor video output supported.\n", devname);
			close(dev->fd);
			return -EINVAL;
		}

		printf("Device %s opened: %s.\n", devname, cap.card);
	} else {
		dev->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		printf("Device %s opened.\n", devname);
	}

	return 0;
}

static void video_close(struct device *dev)
{
	free(dev->mem);
	close(dev->fd);
}

static void uvc_get_control(struct device *dev, unsigned int id)
{
	struct v4l2_control ctrl;
	int ret;

	ctrl.id = id;

	ret = ioctl(dev->fd, VIDIOC_G_CTRL, &ctrl);
	if (ret < 0) {
		printf("unable to get control: %s (%d).\n",
			strerror(errno), errno);
		return;
	}

	printf("Control 0x%08x value %u\n", id, ctrl.value);
}

static void uvc_set_control(struct device *dev, unsigned int id, int value)
{
	struct v4l2_control ctrl;
	int ret;

	ctrl.id = id;
	ctrl.value = value;

	ret = ioctl(dev->fd, VIDIOC_S_CTRL, &ctrl);
	if (ret < 0) {
		printf("unable to set control: %s (%d).\n",
			strerror(errno), errno);
		return;
	}

	printf("Control 0x%08x set to %u, is %u\n", id, value,
		ctrl.value);
}

static int video_get_format(struct device *dev)
{
	struct v4l2_format fmt;
	int ret;

	memset(&fmt, 0, sizeof fmt);
	fmt.type = dev->type;

	ret = ioctl(dev->fd, VIDIOC_G_FMT, &fmt);
	if (ret < 0) {
		printf("Unable to get format: %s (%d).\n", strerror(errno),
			errno);
		return ret;
	}

	printf("Video format: %c%c%c%c (%08x) %ux%u\n",
		(fmt.fmt.pix.pixelformat >> 0) & 0xff,
		(fmt.fmt.pix.pixelformat >> 8) & 0xff,
		(fmt.fmt.pix.pixelformat >> 16) & 0xff,
		(fmt.fmt.pix.pixelformat >> 24) & 0xff,
		fmt.fmt.pix.pixelformat,
		fmt.fmt.pix.width, fmt.fmt.pix.height);
	return 0;
}

static int video_set_format(struct device *dev, unsigned int w, unsigned int h, unsigned int format)
{
	struct v4l2_format fmt;
	int ret;

	memset(&fmt, 0, sizeof fmt);
	fmt.type = dev->type;
	fmt.fmt.pix.width = w;
	fmt.fmt.pix.height = h;
	fmt.fmt.pix.pixelformat = format;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;

	ret = ioctl(dev->fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		printf("Unable to set format: %s (%d).\n", strerror(errno),
			errno);
		return ret;
	}

	printf("Video format set: width: %u height: %u buffer size: %u\n",
		fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.sizeimage);
	return 0;
}

static int video_set_framerate(struct device *dev, struct v4l2_fract *time_per_frame)
{
	struct v4l2_streamparm parm;
	int ret;

	memset(&parm, 0, sizeof parm);
	parm.type = dev->type;

	ret = ioctl(dev->fd, VIDIOC_G_PARM, &parm);
	if (ret < 0) {
		printf("Unable to get frame rate: %d.\n", errno);
		return ret;
	}

	printf("Current frame rate: %u/%u\n",
		parm.parm.capture.timeperframe.numerator,
		parm.parm.capture.timeperframe.denominator);

	printf("Setting frame rate to: %u/%u\n",
		time_per_frame->numerator,
		time_per_frame->denominator);

	parm.parm.capture.timeperframe.numerator = time_per_frame->numerator;
	parm.parm.capture.timeperframe.denominator = time_per_frame->denominator;

	ret = ioctl(dev->fd, VIDIOC_S_PARM, &parm);
	if (ret < 0) {
		printf("Unable to set frame rate: %d.\n", errno);
		return ret;
	}

	ret = ioctl(dev->fd, VIDIOC_G_PARM, &parm);
	if (ret < 0) {
		printf("Unable to get frame rate: %d.\n", errno);
		return ret;
	}

	printf("Frame rate set: %u/%u\n",
		parm.parm.capture.timeperframe.numerator,
		parm.parm.capture.timeperframe.denominator);
	return 0;
}

static int video_alloc_buffers(struct device *dev, int nbufs)
{
	struct v4l2_requestbuffers rb;
	struct v4l2_buffer buf;
	void **bufmem;
	unsigned int i;
	int ret;

	memset(&rb, 0, sizeof rb);
	rb.count = nbufs;
	rb.type = dev->type;
	rb.memory = dev->memtype;

	ret = ioctl(dev->fd, VIDIOC_REQBUFS, &rb);
	if (ret < 0) {
		printf("Unable to request buffers: %d.\n", errno);
		return ret;
	}

	printf("%u buffers requested.\n", rb.count);

	bufmem = malloc(rb.count * sizeof bufmem[0]);
	if (bufmem == NULL)
		return -ENOMEM;

	/* Map the buffers. */
	for (i = 0; i < rb.count; ++i) {
		memset(&buf, 0, sizeof buf);
		buf.index = i;
		buf.type = dev->type;
		buf.memory = dev->memtype;
		ret = ioctl(dev->fd, VIDIOC_QUERYBUF, &buf);
		if (ret < 0) {
			printf("Unable to query buffer %u (%d).\n", i, errno);
			return ret;
		}
		printf("length: %u offset: %u\n", buf.length, buf.m.offset);

		switch (dev->memtype) {
		case V4L2_MEMORY_MMAP:
			bufmem[i] = mmap(0, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, buf.m.offset);
			if (bufmem[i] == MAP_FAILED) {
				printf("Unable to map buffer %u (%d)\n", i, errno);
				return ret;
			}
			printf("Buffer %u mapped at address %p.\n", i, bufmem[i]);
			break;

		case V4L2_MEMORY_USERPTR:
			bufmem[i] = malloc(buf.length);
			if (bufmem[i] == NULL) {
				printf("Unable to allocate buffer %u (%d)\n", i, errno);
				return -ENOMEM;
			}
			printf("Buffer %u allocated at address %p.\n", i, bufmem[i]);
			break;

		default:
			break;
		}
	}

	dev->mem = bufmem;
	dev->nbufs = rb.count;
	dev->bufsize = buf.length;
	return 0;
}

static int video_free_buffers(struct device *dev)
{
	struct v4l2_requestbuffers rb;
	unsigned int i;
	int ret;

	if (dev->nbufs == 0)
		return 0;

	if (dev->memtype == V4L2_MEMORY_MMAP) {
		for (i = 0; i < dev->nbufs; ++i) {
			ret = munmap(dev->mem[i], dev->bufsize);
			if (ret < 0) {
				printf("Unable to unmap buffer %u (%d)\n", i, errno);
				return ret;
			}
		}
	}

	memset(&rb, 0, sizeof rb);
	rb.count = 0;
	rb.type = dev->type;
	rb.memory = dev->memtype;

	ret = ioctl(dev->fd, VIDIOC_REQBUFS, &rb);
	if (ret < 0) {
		printf("Unable to release buffers: %d.\n", errno);
		return ret;
	}

	printf("%u buffers released.\n", dev->nbufs);

	free(dev->mem);
	dev->nbufs = 0;
	dev->mem = NULL;

	return 0;
}

static int video_queue_buffer(struct device *dev, int index)
{
	struct v4l2_buffer buf;
	int ret;

	memset(&buf, 0, sizeof buf);
	buf.index = index;
	buf.type = dev->type;
	buf.memory = dev->memtype;
	if (dev->memtype == V4L2_MEMORY_USERPTR)
		buf.m.userptr = (unsigned long)dev->mem[index];

	if (dev->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		buf.bytesused = buf.length;
		memset(dev->mem[buf.index], 0, buf.bytesused);
	}

	ret = ioctl(dev->fd, VIDIOC_QBUF, &buf);
	if (ret < 0)
		printf("Unable to queue buffer (%d).\n", errno);

	return ret;
}

static int video_enable(struct device *dev, int enable)
{
	int type = dev->type;
	int ret;

	ret = ioctl(dev->fd, enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		printf("Unable to %s streaming: %d.\n", enable ? "start" : "stop",
			errno);
		return ret;
	}

	return 0;
}

static void video_query_menu(struct device *dev, unsigned int id)
{
	struct v4l2_querymenu menu;
	int ret;

	menu.index = 0;
	while (1) {
		menu.id = id;
		ret = ioctl(dev->fd, VIDIOC_QUERYMENU, &menu);
		if (ret < 0)
			break;

		printf("  %u: %.32s\n", menu.index, menu.name);
		menu.index++;
	};
}

static void video_list_controls(struct device *dev)
{
	struct v4l2_queryctrl query;
	struct v4l2_control ctrl;
	unsigned int nctrls = 0;
	char value[12];
	int ret;

#ifndef V4L2_CTRL_FLAG_NEXT_CTRL
	unsigned int i;

	for (i = V4L2_CID_BASE; i <= V4L2_CID_LASTP1; ++i) {
		query.id = i;
#else
	query.id = 0;
	while (1) {
		query.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
#endif
		ret = ioctl(dev->fd, VIDIOC_QUERYCTRL, &query);
		if (ret < 0)
			break;

		if (query.flags & V4L2_CTRL_FLAG_DISABLED)
			continue;

		ctrl.id = query.id;
		ret = ioctl(dev->fd, VIDIOC_G_CTRL, &ctrl);
		if (ret < 0)
			strcpy(value, "n/a");
		else
			sprintf(value, "%d", ctrl.value);

		printf("control 0x%08x %s min %d max %d step %d default %d current %s.\n",
			query.id, query.name, query.minimum, query.maximum,
			query.step, query.default_value, value);

		if (query.type == V4L2_CTRL_TYPE_MENU)
			video_query_menu(dev, query.id);

		nctrls++;
	}

	if (nctrls)
		printf("%u control%s found.\n", nctrls, nctrls > 1 ? "s" : "");
	else
		printf("No control found.\n");
}

static void video_enum_frame_intervals(struct device *dev, __u32 pixelformat,
	unsigned int width, unsigned int height)
{
	struct v4l2_frmivalenum ival;
	unsigned int i;
	int ret;

	for (i = 0; ; ++i) {
		memset(&ival, 0, sizeof ival);
		ival.index = i;
		ival.pixel_format = pixelformat;
		ival.width = width;
		ival.height = height;
		ret = ioctl(dev->fd, VIDIOC_ENUM_FRAMEINTERVALS, &ival);
		if (ret < 0)
			break;

		if (i != ival.index)
			printf("Warning: driver returned wrong ival index "
				"%u.\n", ival.index);
		if (pixelformat != ival.pixel_format)
			printf("Warning: driver returned wrong ival pixel "
				"format %08x.\n", ival.pixel_format);
		if (width != ival.width)
			printf("Warning: driver returned wrong ival width "
				"%u.\n", ival.width);
		if (height != ival.height)
			printf("Warning: driver returned wrong ival height "
				"%u.\n", ival.height);

		if (i != 0)
			printf(", ");

		switch (ival.type) {
		case V4L2_FRMIVAL_TYPE_DISCRETE:
			printf("%u/%u",
				ival.discrete.numerator,
				ival.discrete.denominator);
			break;

		case V4L2_FRMIVAL_TYPE_CONTINUOUS:
			printf("%u/%u - %u/%u",
				ival.stepwise.min.numerator,
				ival.stepwise.min.denominator,
				ival.stepwise.max.numerator,
				ival.stepwise.max.denominator);
			return;

		case V4L2_FRMIVAL_TYPE_STEPWISE:
			printf("%u/%u - %u/%u (by %u/%u)",
				ival.stepwise.min.numerator,
				ival.stepwise.min.denominator,
				ival.stepwise.max.numerator,
				ival.stepwise.max.denominator,
				ival.stepwise.step.numerator,
				ival.stepwise.step.denominator);
			return;

		default:
			break;
		}
	}
}

static void video_enum_frame_sizes(struct device *dev, __u32 pixelformat)
{
	struct v4l2_frmsizeenum frame;
	unsigned int i;
	int ret;

	for (i = 0; ; ++i) {
		memset(&frame, 0, sizeof frame);
		frame.index = i;
		frame.pixel_format = pixelformat;
		ret = ioctl(dev->fd, VIDIOC_ENUM_FRAMESIZES, &frame);
		if (ret < 0)
			break;

		if (i != frame.index)
			printf("Warning: driver returned wrong frame index "
				"%u.\n", frame.index);
		if (pixelformat != frame.pixel_format)
			printf("Warning: driver returned wrong frame pixel "
				"format %08x.\n", frame.pixel_format);

		switch (frame.type) {
		case V4L2_FRMSIZE_TYPE_DISCRETE:
			printf("\tFrame size: %ux%u (", frame.discrete.width,
				frame.discrete.height);
			video_enum_frame_intervals(dev, frame.pixel_format,
				frame.discrete.width, frame.discrete.height);
			printf(")\n");
			break;

		case V4L2_FRMSIZE_TYPE_CONTINUOUS:
			printf("\tFrame size: %ux%u - %ux%u (",
				frame.stepwise.min_width,
				frame.stepwise.min_height,
				frame.stepwise.max_width,
				frame.stepwise.max_height);
			video_enum_frame_intervals(dev, frame.pixel_format,
				frame.stepwise.max_width,
				frame.stepwise.max_height);
			printf(")\n");
			break;

		case V4L2_FRMSIZE_TYPE_STEPWISE:
			printf("\tFrame size: %ux%u - %ux%u (by %ux%u) (\n",
				frame.stepwise.min_width,
				frame.stepwise.min_height,
				frame.stepwise.max_width,
				frame.stepwise.max_height,
				frame.stepwise.step_width,
				frame.stepwise.step_height);
			video_enum_frame_intervals(dev, frame.pixel_format,
				frame.stepwise.max_width,
				frame.stepwise.max_height);
			printf(")\n");
			break;

		default:
			break;
		}
	}
}
static void video_enum_formats(struct device *dev, enum v4l2_buf_type type)
{
	struct v4l2_fmtdesc fmt;
	unsigned int i;
	int ret;

	for (i = 0; ; ++i) {
		memset(&fmt, 0, sizeof fmt);
		fmt.index = i;
		fmt.type = type;
		ret = ioctl(dev->fd, VIDIOC_ENUM_FMT, &fmt);
		if (ret < 0)
			break;

		if (i != fmt.index)
			printf("Warning: driver returned wrong format index "
				"%u.\n", fmt.index);
		if (type != fmt.type)
			printf("Warning: driver returned wrong format type "
				"%u.\n", fmt.type);

		printf("\tFormat %u: %s (%08x)\n", i,
			v4l2_fourcc_name(fmt.pixelformat), fmt.pixelformat);
		printf("\tType: %s (%u)\n", v4l2_buf_type_name(fmt.type),
			fmt.type);
		printf("\tName: %.32s\n", fmt.description);
		video_enum_frame_sizes(dev, fmt.pixelformat);
		printf("\n");
	}
}

static void video_enum_inputs(struct device *dev)
{
	struct v4l2_input input;
	unsigned int i;
	int ret;

	for (i = 0; ; ++i) {
		memset(&input, 0, sizeof input);
		input.index = i;
		ret = ioctl(dev->fd, VIDIOC_ENUMINPUT, &input);
		if (ret < 0)
			break;

		if (i != input.index)
			printf("Warning: driver returned wrong input index "
				"%u.\n", input.index);

		printf("\tInput %u: %s.\n", i, input.name);
	}

	printf("\n");
}

static int video_get_input(struct device *dev)
{
	__u32 input;
	int ret;

	ret = ioctl(dev->fd, VIDIOC_G_INPUT, &input);
	if (ret < 0) {
		printf("Unable to get current input: %s.\n", strerror(errno));
		return ret;
	}

	return input;
}

static int video_set_input(struct device *dev, unsigned int input)
{
	__u32 _input = input;
	int ret;

	ret = ioctl(dev->fd, VIDIOC_S_INPUT, &_input);
	if (ret < 0)
		printf("Unable to select input %u: %s.\n", input,
			strerror(errno));

	return ret;
}

static int video_set_quality(struct device *dev, unsigned int quality)
{
	struct v4l2_jpegcompression jpeg;
	int ret;

	if (quality == (unsigned int)-1)
		return 0;

	memset(&jpeg, 0, sizeof jpeg);
	jpeg.quality = quality;

	ret = ioctl(dev->fd, VIDIOC_S_JPEGCOMP, &jpeg);
	if (ret < 0) {
		printf("Unable to set quality to %u: %s.\n", quality,
			strerror(errno));
		return ret;
	}

	ret = ioctl(dev->fd, VIDIOC_G_JPEGCOMP, &jpeg);
	if (ret >= 0)
		printf("Quality set to %u\n", jpeg.quality);

	return 0;
}

static int video_prepare_capture(struct device *dev, int nbufs)
{
	unsigned int i;
	int ret;

	/* Allocate and map buffers. */
	if ((ret = video_alloc_buffers(dev, nbufs)) < 0)
		return ret;

	/* Queue the buffers. */
	for (i = 0; i < dev->nbufs; ++i) {
		ret = video_queue_buffer(dev, i);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int video_do_capture(struct device *dev, unsigned int nframes,
	unsigned int skip, unsigned int delay, const char *filename_prefix)
{
	char *filename;
	struct timeval start, end, ts;
	struct v4l2_buffer buf;
	unsigned int size;
	unsigned int i;
	FILE *file;
	double bps;
	double fps;
	int ret;

	if (filename_prefix != NULL) {
		filename = malloc(strlen(filename_prefix) + 12);
		if (filename == NULL)
			return -ENOMEM;
	}

	/* Start streaming. */
	video_enable(dev, 1);

	size = 0;

	for (i = 0; i < nframes; ++i) {
		/* Dequeue a buffer. */
		memset(&buf, 0, sizeof buf);
		buf.type = dev->type;
		buf.memory = dev->memtype;
		ret = ioctl(dev->fd, VIDIOC_DQBUF, &buf);
		if (ret < 0) {
			if (errno != EIO) {
				printf("Unable to dequeue buffer (%d).\n", errno);
				goto done;
			}
			buf.type = dev->type;
			buf.memory = dev->memtype;
			if (dev->memtype == V4L2_MEMORY_USERPTR)
				buf.m.userptr = (unsigned long)dev->mem[i];
		}

		size += buf.bytesused;

		gettimeofday(&ts, NULL);
		printf("%u (%u) %u bytes %ld.%06ld %ld.%06ld\n", i, buf.index,
			buf.bytesused, buf.timestamp.tv_sec,
			buf.timestamp.tv_usec, ts.tv_sec, ts.tv_usec);

		if (i == 0)
			start = ts;

		/* Save the image. */
		if (dev->type == V4L2_BUF_TYPE_VIDEO_CAPTURE && filename_prefix && !skip) {
			sprintf(filename, "%s-%06u.bin", filename_prefix, i);
			file = fopen(filename, "wb");
			if (file != NULL) {
				ret = fwrite(dev->mem[buf.index], buf.bytesused, 1, file);
				fclose(file);
			}
		}
		if (skip)
			--skip;

		/* Requeue the buffer. */
		if (delay > 0)
			usleep(delay * 1000);

		ret = video_queue_buffer(dev, buf.index);
		if (ret < 0) {
			printf("Unable to requeue buffer (%d).\n", errno);
			goto done;
		}

		fflush(stdout);
	}
	gettimeofday(&end, NULL);

	/* Stop streaming. */
	video_enable(dev, 0);

	end.tv_sec -= start.tv_sec;
	end.tv_usec -= start.tv_usec;
	if (end.tv_usec < 0) {
		end.tv_sec--;
		end.tv_usec += 1000000;
	}

	bps = size/(end.tv_usec+1000000.0*end.tv_sec)*1000000.0;
	fps = (i-1)/(end.tv_usec+1000000.0*end.tv_sec)*1000000.0;

	printf("Captured %u frames in %lu.%06lu seconds (%f fps, %f B/s).\n",
		i-1, end.tv_sec, end.tv_usec, fps, bps);

done:
	free(filename);
	return video_free_buffers(dev);
}

#define V4L_BUFFERS_DEFAULT	8
#define V4L_BUFFERS_MAX		32

static void usage(const char *argv0)
{
	printf("Usage: %s [options] device\n", argv0);
	printf("Supported options:\n");
	printf("-c, --capture[=nframes]		Capture frames\n");
	printf("-d, --delay			Delay (in ms) before requeuing buffers\n");
	printf("-f, --format format		Set the video format\n");
	printf("-F, --file prefix		Read/write frames from/to disk\n");
	printf("-h, --help			Show this help screen\n");
	printf("-i, --input input		Select the video input\n");
	printf("-l, --list-controls		List available controls\n");
	printf("-n, --nbufs n			Set the number of video buffers\n");
	printf("-p, --pause			Pause before starting the video stream\n");
	printf("-q, --quality n			MJPEG quality (0-100)\n");
	printf("-r, --get-control ctrl		Get control 'ctrl'\n");
	printf("-s, --size WxH			Set the frame size\n");
	printf("-t, --time-per-frame num/denom	Set the time per frame (eg. 1/25 = 25 fps)\n");
	printf("-u, --userptr			Use the user pointers streaming method\n");
	printf("-w, --set-control 'ctrl value'	Set control 'ctrl' to 'value'\n");
	printf("    --enum-formats		Enumerate formats\n");
	printf("    --enum-inputs		Enumerate inputs\n");
	printf("    --no-query			Don't query capabilities on open\n");
	printf("    --skip n			Skip the first n frames\n");
	printf("    --sleep-forever		Sleep forever after configuring the device\n");
}

#define OPT_ENUM_FORMATS	256
#define OPT_ENUM_INPUTS		257
#define OPT_SKIP_FRAMES		258
#define OPT_NO_QUERY		259
#define OPT_SLEEP_FOREVER	260

static struct option opts[] = {
	{"capture", 2, 0, 'c'},
	{"delay", 1, 0, 'd'},
	{"enum-formats", 0, 0, OPT_ENUM_FORMATS},
	{"enum-inputs", 0, 0, OPT_ENUM_INPUTS},
	{"file", 2, 0, 'F'},
	{"format", 1, 0, 'f'},
	{"help", 0, 0, 'h'},
	{"input", 1, 0, 'i'},
	{"list-controls", 0, 0, 'l'},
	{"nbufs", 1, 0, 'n'},
	{"no-query", 0, 0, OPT_NO_QUERY},
	{"pause", 0, 0, 'p'},
	{"quality", 1, 0, 'q'},
	{"get-control", 1, 0, 'r'},
	{"size", 1, 0, 's'},
	{"set-control", 1, 0, 'w'},
	{"skip", 1, 0, OPT_SKIP_FRAMES},
	{"sleep-forever", 0, 0, OPT_SLEEP_FOREVER},
	{"time-per-frame", 1, 0, 't'},
	{"userptr", 0, 0, 'u'},
	{0, 0, 0, 0}
};

int main(int argc, char *argv[])
{
	struct device dev;
	int ret;

	/* Options parsings */
	int do_file = 0, do_capture = 0, do_pause = 0;
	int do_set_time_per_frame = 0;
	int do_enum_formats = 0, do_set_format = 0;
	int do_enum_inputs = 0, do_set_input = 0;
	int do_list_controls = 0, do_get_control = 0, do_set_control = 0;
	int do_sleep_forever = 0;
	int no_query = 0;
	char *endptr;
	int c;

	/* Controls */
	int ctrl_name = 0;
	int ctrl_value = 0;

	/* Video buffers */
	enum v4l2_memory memtype = V4L2_MEMORY_MMAP;
	unsigned int pixelformat = V4L2_PIX_FMT_YUYV;
	unsigned int width = 640;
	unsigned int height = 480;
	unsigned int nbufs = V4L_BUFFERS_DEFAULT;
	unsigned int input = 0;
	unsigned int skip = 0;
	unsigned int quality = (unsigned int)-1;
	struct v4l2_fract time_per_frame = {1, 25};

	/* Capture loop */
	unsigned int delay = 0, nframes = (unsigned int)-1;
	const char *filename = "frame";

	opterr = 0;
	while ((c = getopt_long(argc, argv, "cd:f:Fhi:ln:pq:r:s:t:uw:", opts, NULL)) != -1) {

		switch (c) {
		case 'c':
			do_capture = 1;
			if (optarg)
				nframes = atoi(optarg);
			break;
		case 'd':
			delay = atoi(optarg);
			break;
		case 'f':
			do_set_format = 1;
			if (strcasecmp(optarg, "MJPEG") == 0)
				pixelformat = V4L2_PIX_FMT_MJPEG;
			else if (strcasecmp(optarg, "YUYV") == 0)
				pixelformat = V4L2_PIX_FMT_YUYV;
			else if (strcasecmp(optarg, "UYVY") == 0)
				pixelformat = V4L2_PIX_FMT_UYVY;
			else if (strcasecmp(optarg, "SGRBG10") == 0)
				pixelformat = V4L2_PIX_FMT_SGRBG10;
			else if (strcasecmp(optarg, "DV") == 0)
				pixelformat = V4L2_PIX_FMT_DV;
			else {
				printf("Unsupported video format '%s'\n", optarg);
				return 1;
			}
			break;
		case 'F':
			do_file = 1;
			if (optarg)
				filename = optarg;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		case 'i':
			do_set_input = 1;
			input = atoi(optarg);
			break;
		case 'l':
			do_list_controls = 1;
			break;
		case 'n':
			nbufs = atoi(optarg);
			if (nbufs > V4L_BUFFERS_MAX)
				nbufs = V4L_BUFFERS_MAX;
			break;
		case 'p':
			do_pause = 1;
			break;
		case 'q':
			quality = atoi(optarg);
			break;
		case 'r':
			ctrl_name = strtol(optarg, &endptr, 0);
			if (*endptr != 0) {
				printf("Invalid control name '%s'\n", optarg);
				return 1;
			}
			do_get_control = 1;
			break;
		case 's':
			do_set_format = 1;
			width = strtol(optarg, &endptr, 10);
			if (*endptr != 'x' || endptr == optarg) {
				printf("Invalid size '%s'\n", optarg);
				return 1;
			}
			height = strtol(endptr + 1, &endptr, 10);
			if (*endptr != 0) {
				printf("Invalid size '%s'\n", optarg);
				return 1;
			}
			break;
		case 't':
			do_set_time_per_frame = 1;
			time_per_frame.numerator = strtol(optarg, &endptr, 10);
			if (*endptr != '/' || endptr == optarg) {
				printf("Invalid time per frame '%s'\n", optarg);
				return 1;
			}
			time_per_frame.denominator = strtol(endptr + 1, &endptr, 10);
			if (*endptr != 0) {
				printf("Invalid time per frame '%s'\n", optarg);
				return 1;
			}
			break;
		case 'u':
			memtype = V4L2_MEMORY_USERPTR;
			break;
		case 'w':
			ctrl_name = strtol(optarg, &endptr, 0);
			if (*endptr != ' ' || endptr == optarg) {
				printf("Invalid control name '%s'\n", optarg);
				return 1;
			}
			ctrl_value = strtol(endptr + 1, &endptr, 0);
			if (*endptr != 0) {
				printf("Invalid control value '%s'\n", optarg);
				return 1;
			}
			do_set_control = 1;
			break;
		case OPT_ENUM_FORMATS:
			do_enum_formats = 1;
			break;
		case OPT_ENUM_INPUTS:
			do_enum_inputs = 1;
			break;
		case OPT_NO_QUERY:
			no_query = 1;
			break;
		case OPT_SKIP_FRAMES:
			skip = atoi(optarg);
			break;
		case OPT_SLEEP_FOREVER:
			do_sleep_forever = 1;
			break;
		default:
			printf("Invalid option -%c\n", c);
			printf("Run %s -h for help.\n", argv[0]);
			return 1;
		}
	}

	if (optind >= argc) {
		usage(argv[0]);
		return 1;
	}

	if (!do_file)
		filename = NULL;

	/* Open the video device. */
	ret = video_open(&dev, argv[optind], no_query);
	if (ret < 0)
		return 1;

	dev.memtype = memtype;

	if (do_get_control)
		uvc_get_control(&dev, ctrl_name);
	if (do_set_control)
		uvc_set_control(&dev, ctrl_name, ctrl_value);

	if (do_list_controls)
		video_list_controls(&dev);

	if (do_enum_formats) {
		printf("- Available formats:\n");
		video_enum_formats(&dev, V4L2_BUF_TYPE_VIDEO_CAPTURE);
		video_enum_formats(&dev, V4L2_BUF_TYPE_VIDEO_OUTPUT);
		video_enum_formats(&dev, V4L2_BUF_TYPE_VIDEO_OVERLAY);
	}

	if (do_enum_inputs) {
		printf("- Available inputs:\n");
		video_enum_inputs(&dev);
	}

	if (do_set_input) {
		video_set_input(&dev, input);
		ret = video_get_input(&dev);
		printf("Input %d selected\n", ret);
	}

	/* Set the video format. */
	if (do_set_format) {
		if (video_set_format(&dev, width, height, pixelformat) < 0) {
			video_close(&dev);
			return 1;
		}
	}

	if (!no_query)
		video_get_format(&dev);

	/* Set the frame rate. */
	if (do_set_time_per_frame) {
		if (video_set_framerate(&dev, &time_per_frame) < 0) {
			video_close(&dev);
			return 1;
		}
	}

	while (do_sleep_forever)
		sleep(1000);

	if (!do_capture) {
		video_close(&dev);
		return 0;
	}

	/* Set the compression quality. */
	if (video_set_quality(&dev, quality) < 0) {
		video_close(&dev);
		return 1;
	}

	if (video_prepare_capture(&dev, nbufs)) {
		video_close(&dev);
		return 1;
	}

	if (do_pause) {
		printf("Press enter to start capture\n");
		getchar();
	}

	if (video_do_capture(&dev, nframes, skip, delay, filename) < 0) {
		video_close(&dev);
		return 1;
	}

	video_close(&dev);
	return 0;
}

