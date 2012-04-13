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

#define __STDC_FORMAT_MACROS

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <sched.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "videodev2.h"

#ifndef V4L2_BUF_FLAG_ERROR
#define V4L2_BUF_FLAG_ERROR	0x0040
#endif

#define ARRAY_SIZE(a)	(sizeof(a)/sizeof((a)[0]))

enum buffer_fill_mode
{
	BUFFER_FILL_NONE = 0,
	BUFFER_FILL_FRAME = 1 << 0,
	BUFFER_FILL_PADDING = 1 << 1,
};

struct buffer
{
	unsigned int padding;
	unsigned int size;
	void *mem;
};

struct device
{
	int fd;

	enum v4l2_buf_type type;
	enum v4l2_memory memtype;
	unsigned int nbufs;
	struct buffer *buffers;

	unsigned int width;
	unsigned int height;
	unsigned int bytesperline;
	unsigned int imagesize;

	void *pattern;
	unsigned int patternsize;
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

static struct {
	const char *name;
	unsigned int fourcc;
} pixel_formats[] = {
	{ "RGB332", V4L2_PIX_FMT_RGB332 },
	{ "RGB555", V4L2_PIX_FMT_RGB555 },
	{ "RGB565", V4L2_PIX_FMT_RGB565 },
	{ "RGB555X", V4L2_PIX_FMT_RGB555X },
	{ "RGB565X", V4L2_PIX_FMT_RGB565X },
	{ "BGR24", V4L2_PIX_FMT_BGR24 },
	{ "RGB24", V4L2_PIX_FMT_RGB24 },
	{ "BGR32", V4L2_PIX_FMT_BGR32 },
	{ "RGB32", V4L2_PIX_FMT_RGB32 },
	{ "Y8", V4L2_PIX_FMT_GREY },
	{ "Y10", V4L2_PIX_FMT_Y10 },
	{ "Y12", V4L2_PIX_FMT_Y12 },
	{ "Y16", V4L2_PIX_FMT_Y16 },
	{ "YUYV", V4L2_PIX_FMT_YUYV },
	{ "UYVY", V4L2_PIX_FMT_UYVY },
	{ "NV12", V4L2_PIX_FMT_NV12 },
	{ "NV21", V4L2_PIX_FMT_NV21 },
	{ "NV16", V4L2_PIX_FMT_NV16 },
	{ "NV61", V4L2_PIX_FMT_NV61 },
	{ "NV24", V4L2_PIX_FMT_NV24 },
	{ "NV42", V4L2_PIX_FMT_NV42 },
	{ "SBGGR8", V4L2_PIX_FMT_SBGGR8 },
	{ "SGBRG8", V4L2_PIX_FMT_SGBRG8 },
	{ "SGRBG8", V4L2_PIX_FMT_SGRBG8 },
	{ "SRGGB8", V4L2_PIX_FMT_SRGGB8 },
	{ "SGRBG10_DPCM8", V4L2_PIX_FMT_SGRBG10DPCM8 },
	{ "SBGGR10", V4L2_PIX_FMT_SBGGR10 },
	{ "SGBRG10", V4L2_PIX_FMT_SGBRG10 },
	{ "SGRBG10", V4L2_PIX_FMT_SGRBG10 },
	{ "SRGGB10", V4L2_PIX_FMT_SRGGB10 },
	{ "SBGGR12", V4L2_PIX_FMT_SBGGR12 },
	{ "SGBRG12", V4L2_PIX_FMT_SGBRG12 },
	{ "SGRBG12", V4L2_PIX_FMT_SGRBG12 },
	{ "SRGGB12", V4L2_PIX_FMT_SRGGB12 },
	{ "DV", V4L2_PIX_FMT_DV },
	{ "MJPEG", V4L2_PIX_FMT_MJPEG },
	{ "MPEG", V4L2_PIX_FMT_MPEG },
};

static const char *v4l2_format_name(unsigned int fourcc)
{
	static char name[5];
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pixel_formats); ++i) {
		if (pixel_formats[i].fourcc == fourcc)
			return pixel_formats[i].name;
	}

	for (i = 0; i < 4; ++i) {
		name[i] = fourcc & 0xff;
		fourcc >>= 8;
	}

	name[4] = '\0';
	return name;
}

static unsigned int v4l2_format_code(const char *name)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pixel_formats); ++i) {
		if (strcasecmp(pixel_formats[i].name, name) == 0)
			return pixel_formats[i].fourcc;
	}

	return 0;
}

static int video_open(struct device *dev, const char *devname, int no_query)
{
	struct v4l2_capability cap;
	int ret;

	memset(dev, 0, sizeof *dev);
	dev->fd = -1;
	dev->memtype = V4L2_MEMORY_MMAP;
	dev->buffers = NULL;
	dev->type = (enum v4l2_buf_type)-1;

	dev->fd = open(devname, O_RDWR);
	if (dev->fd < 0) {
		printf("Error opening device %s: %s (%d).\n", devname,
		       strerror(errno), errno);
		return dev->fd;
	}

	printf("Device %s opened.\n", devname);

	if (no_query) {
		/* Assume capture device. */
		dev->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		return 0;
	}

	memset(&cap, 0, sizeof cap);
	ret = ioctl(dev->fd, VIDIOC_QUERYCAP, &cap);
	if (ret < 0)
		return 0;

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

	printf("Device `%s' on `%s' is a video %s device.\n",
		cap.card, cap.bus_info,
		dev->type == V4L2_BUF_TYPE_VIDEO_CAPTURE ? "capture" : "output");
	return 0;
}

static void video_close(struct device *dev)
{
	free(dev->pattern);
	free(dev->buffers);
	close(dev->fd);
}

static unsigned int get_control_type(struct device *dev, unsigned int id)
{
	struct v4l2_queryctrl query;
	int ret;

	memset(&query, 0, sizeof(query));

	query.id = id;
	ret = ioctl(dev->fd, VIDIOC_QUERYCTRL, &query);
	if (ret == -1)
		return V4L2_CTRL_TYPE_INTEGER;

	return query.type;
}

static int get_control(struct device *dev, unsigned int id, int type,
		       int64_t *val)
{
	struct v4l2_ext_controls ctrls;
	struct v4l2_ext_control ctrl;
	int ret;

	memset(&ctrls, 0, sizeof(ctrls));
	memset(&ctrl, 0, sizeof(ctrl));

	ctrls.ctrl_class = V4L2_CTRL_ID2CLASS(id);
	ctrls.count = 1;
	ctrls.controls = &ctrl;

	ctrl.id = id;

	ret = ioctl(dev->fd, VIDIOC_G_EXT_CTRLS, &ctrls);
	if (ret != -1) {
		if (type == V4L2_CTRL_TYPE_INTEGER64)
			*val = ctrl.value64;
		else
			*val = ctrl.value;
		return 0;
	}
	if (errno == EINVAL || errno == ENOTTY) {
		struct v4l2_control old;

		old.id = id;
		ret = ioctl(dev->fd, VIDIOC_G_CTRL, &old);
		if (ret != -1) {
			*val = old.value;
			return 0;
		}
	}

	printf("unable to get control 0x%8.8x: %s (%d).\n",
		id, strerror(errno), errno);
	return -1;
}

static void set_control(struct device *dev, unsigned int id, int type,
		        int64_t val)
{
	struct v4l2_ext_controls ctrls;
	struct v4l2_ext_control ctrl;
	int is_64 = type == V4L2_CTRL_TYPE_INTEGER64;
	int64_t old_val = val;
	int ret;

	memset(&ctrls, 0, sizeof(ctrls));
	memset(&ctrl, 0, sizeof(ctrl));

	ctrls.ctrl_class = V4L2_CTRL_ID2CLASS(id);
	ctrls.count = 1;
	ctrls.controls = &ctrl;

	ctrl.id = id;
	if (is_64)
		ctrl.value64 = val;
	else
		ctrl.value = val;

	ret = ioctl(dev->fd, VIDIOC_S_EXT_CTRLS, &ctrls);
	if (ret != -1) {
		if (is_64)
			val = ctrl.value64;
		else
			val = ctrl.value;
	} else if (errno == EINVAL || errno == ENOTTY) {
		struct v4l2_control old;

		old.id = id;
		ret = ioctl(dev->fd, VIDIOC_S_CTRL, &old);
		if (ret != -1)
			val = old.value;
	}
	if (ret == -1) {
		printf("unable to set control 0x%8.8x: %s (%d).\n",
			id, strerror(errno), errno);
		return;
	}

	printf("Control 0x%08x set to %" PRId64 ", is %" PRId64 "\n",
	       id, old_val, val);
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

	dev->width = fmt.fmt.pix.width;
	dev->height = fmt.fmt.pix.height;
	dev->bytesperline = fmt.fmt.pix.bytesperline;
	dev->imagesize = fmt.fmt.pix.bytesperline ? fmt.fmt.pix.sizeimage : 0;

	printf("Video format: %s (%08x) %ux%u (stride %u) buffer size %u\n",
		v4l2_format_name(fmt.fmt.pix.pixelformat), fmt.fmt.pix.pixelformat,
		fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.bytesperline,
		fmt.fmt.pix.sizeimage);
	return 0;
}

static int video_set_format(struct device *dev, unsigned int w, unsigned int h,
			    unsigned int format, unsigned int stride)
{
	struct v4l2_format fmt;
	int ret;

	memset(&fmt, 0, sizeof fmt);
	fmt.type = dev->type;
	fmt.fmt.pix.width = w;
	fmt.fmt.pix.height = h;
	fmt.fmt.pix.pixelformat = format;
	fmt.fmt.pix.bytesperline = stride;
	fmt.fmt.pix.field = V4L2_FIELD_ANY;

	ret = ioctl(dev->fd, VIDIOC_S_FMT, &fmt);
	if (ret < 0) {
		printf("Unable to set format: %s (%d).\n", strerror(errno),
			errno);
		return ret;
	}

	printf("Video format set: %s (%08x) %ux%u (stride %u) buffer size %u\n",
		v4l2_format_name(fmt.fmt.pix.pixelformat), fmt.fmt.pix.pixelformat,
		fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.bytesperline,
		fmt.fmt.pix.sizeimage);
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
		printf("Unable to get frame rate: %s (%d).\n",
			strerror(errno), errno);
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
		printf("Unable to set frame rate: %s (%d).\n", strerror(errno),
			errno);
		return ret;
	}

	ret = ioctl(dev->fd, VIDIOC_G_PARM, &parm);
	if (ret < 0) {
		printf("Unable to get frame rate: %s (%d).\n", strerror(errno),
			errno);
		return ret;
	}

	printf("Frame rate set: %u/%u\n",
		parm.parm.capture.timeperframe.numerator,
		parm.parm.capture.timeperframe.denominator);
	return 0;
}

static int video_alloc_buffers(struct device *dev, int nbufs,
	unsigned int offset, unsigned int padding)
{
	struct v4l2_requestbuffers rb;
	struct v4l2_buffer buf;
	int page_size;
	struct buffer *buffers;
	unsigned int i;
	int ret;

	memset(&rb, 0, sizeof rb);
	rb.count = nbufs;
	rb.type = dev->type;
	rb.memory = dev->memtype;

	ret = ioctl(dev->fd, VIDIOC_REQBUFS, &rb);
	if (ret < 0) {
		printf("Unable to request buffers: %s (%d).\n", strerror(errno),
			errno);
		return ret;
	}

	printf("%u buffers requested.\n", rb.count);

	buffers = malloc(rb.count * sizeof buffers[0]);
	if (buffers == NULL)
		return -ENOMEM;

	page_size = getpagesize();

	/* Map the buffers. */
	for (i = 0; i < rb.count; ++i) {
		memset(&buf, 0, sizeof buf);
		buf.index = i;
		buf.type = dev->type;
		buf.memory = dev->memtype;
		ret = ioctl(dev->fd, VIDIOC_QUERYBUF, &buf);
		if (ret < 0) {
			printf("Unable to query buffer %u: %s (%d).\n", i,
				strerror(errno), errno);
			return ret;
		}
		printf("length: %u offset: %u\n", buf.length, buf.m.offset);

		switch (dev->memtype) {
		case V4L2_MEMORY_MMAP:
			buffers[i].mem = mmap(0, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, dev->fd, buf.m.offset);
			if (buffers[i].mem == MAP_FAILED) {
				printf("Unable to map buffer %u: %s (%d)\n", i,
					strerror(errno), errno);
				return ret;
			}
			buffers[i].size = buf.length;
			buffers[i].padding = 0;
			printf("Buffer %u mapped at address %p.\n", i, buffers[i].mem);
			break;

		case V4L2_MEMORY_USERPTR:
			ret = posix_memalign(&buffers[i].mem, page_size, buf.length + offset + padding);
			if (ret < 0) {
				printf("Unable to allocate buffer %u (%d)\n", i, ret);
				return -ENOMEM;
			}
			buffers[i].mem += offset;
			buffers[i].size = buf.length;
			buffers[i].padding = padding;
			printf("Buffer %u allocated at address %p.\n", i, buffers[i].mem);
			break;

		default:
			break;
		}
	}

	dev->buffers = buffers;
	dev->nbufs = rb.count;
	return 0;
}

static int video_free_buffers(struct device *dev)
{
	struct v4l2_requestbuffers rb;
	unsigned int i;
	int ret;

	if (dev->nbufs == 0)
		return 0;

	for (i = 0; i < dev->nbufs; ++i) {
		switch (dev->memtype) {
		case V4L2_MEMORY_MMAP:
			ret = munmap(dev->buffers[i].mem, dev->buffers[i].size);
			if (ret < 0) {
				printf("Unable to unmap buffer %u: %s (%d)\n", i,
					strerror(errno), errno);
				return ret;
			}
			break;

		case V4L2_MEMORY_USERPTR:
			free(dev->buffers[i].mem);
			break;

		default:
			break;
		}

		dev->buffers[i].mem = NULL;
	}

	memset(&rb, 0, sizeof rb);
	rb.count = 0;
	rb.type = dev->type;
	rb.memory = dev->memtype;

	ret = ioctl(dev->fd, VIDIOC_REQBUFS, &rb);
	if (ret < 0) {
		printf("Unable to release buffers: %s (%d).\n",
			strerror(errno), errno);
		return ret;
	}

	printf("%u buffers released.\n", dev->nbufs);

	free(dev->buffers);
	dev->nbufs = 0;
	dev->buffers = NULL;

	return 0;
}

static int video_queue_buffer(struct device *dev, int index, enum buffer_fill_mode fill)
{
	struct v4l2_buffer buf;
	int ret;

	memset(&buf, 0, sizeof buf);
	buf.index = index;
	buf.type = dev->type;
	buf.memory = dev->memtype;
	buf.length = dev->buffers[index].size;
	if (dev->memtype == V4L2_MEMORY_USERPTR)
		buf.m.userptr = (unsigned long)dev->buffers[index].mem;

	if (dev->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		buf.bytesused = dev->patternsize;
		memcpy(dev->buffers[buf.index].mem, dev->pattern, dev->patternsize);
	} else {
		if (fill & BUFFER_FILL_FRAME)
			memset(dev->buffers[buf.index].mem, 0x55, dev->buffers[index].size);
		if (fill & BUFFER_FILL_PADDING)
			memset(dev->buffers[buf.index].mem + dev->buffers[index].size,
			       0x55, dev->buffers[index].padding);
	}

	ret = ioctl(dev->fd, VIDIOC_QBUF, &buf);
	if (ret < 0)
		printf("Unable to queue buffer: %s (%d).\n",
			strerror(errno), errno);

	return ret;
}

static int video_enable(struct device *dev, int enable)
{
	int type = dev->type;
	int ret;

	ret = ioctl(dev->fd, enable ? VIDIOC_STREAMON : VIDIOC_STREAMOFF, &type);
	if (ret < 0) {
		printf("Unable to %s streaming: %s (%d).\n",
			enable ? "start" : "stop", strerror(errno), errno);
		return ret;
	}

	return 0;
}

static void video_query_menu(struct device *dev, struct v4l2_queryctrl *query)
{
	struct v4l2_querymenu menu;
	int ret;

	for (menu.index = query->minimum;
	     menu.index <= (unsigned)query->maximum; menu.index++) {
		menu.id = query->id;
		ret = ioctl(dev->fd, VIDIOC_QUERYMENU, &menu);
		if (ret < 0)
			continue;

		if (query->type == V4L2_CTRL_TYPE_MENU)
			printf("  %u: %.32s\n", menu.index, menu.name);
		else
			printf("  %u: %lld\n", menu.index, menu.value);
	};
}

static void video_list_controls(struct device *dev)
{
	struct v4l2_queryctrl query;
	unsigned int nctrls = 0;
	char value[24];
	int64_t val64;
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

		if (query.type == V4L2_CTRL_TYPE_CTRL_CLASS) {
			printf("--- %s (class 0x%08x) ---\n", query.name, query.id);
			continue;
		}

		ret = get_control(dev, query.id, query.type, &val64);
		if (ret < 0)
			strcpy(value, "n/a");
		else
			sprintf(value, "%" PRId64, val64);

		printf("control 0x%08x `%s' min %d max %d step %d default %d current %s.\n",
			query.id, query.name, query.minimum, query.maximum,
			query.step, query.default_value, value);

		if (query.type == V4L2_CTRL_TYPE_MENU ||
		    query.type == V4L2_CTRL_TYPE_INTEGER_MENU)
			video_query_menu(dev, &query);

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
			v4l2_format_name(fmt.pixelformat), fmt.pixelformat);
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
		printf("Unable to get current input: %s (%d).\n",
			strerror(errno), errno);
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
		printf("Unable to select input %u: %s (%d).\n", input,
			strerror(errno), errno);

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
		printf("Unable to set quality to %u: %s (%d).\n", quality,
			strerror(errno), errno);
		return ret;
	}

	ret = ioctl(dev->fd, VIDIOC_G_JPEGCOMP, &jpeg);
	if (ret >= 0)
		printf("Quality set to %u\n", jpeg.quality);

	return 0;
}

static int video_load_test_pattern(struct device *dev, const char *filename)
{
	unsigned int size = dev->buffers[0].size;
	unsigned int x, y;
	uint8_t *data;
	int ret;
	int fd;

	/* Load or generate the test pattern */
	dev->pattern = malloc(size);
	if (dev->pattern == NULL)
		return -ENOMEM;

	if (filename == NULL) {
		if (dev->bytesperline == 0) {
			printf("Compressed format detect and no test pattern filename given.\n"
				"The test pattern can't be generated automatically.\n");
			return -EINVAL;
		}

		data = dev->pattern;

		for (y = 0; y < dev->height; ++y) {
			for (x = 0; x < dev->bytesperline; ++x)
				*data++ = x + y;
		}

		return 0;
	}

	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		printf("Unable to open test pattern file '%s': %s (%d).\n",
			filename, strerror(errno), errno);
		return -errno;
	}

	ret = read(fd, dev->pattern, size);
	close(fd);

	if (ret != (int)size && dev->bytesperline != 0) {
		printf("Test pattern file size %u doesn't match image size %u\n",
			ret, size);
		return -EINVAL;
	}

	dev->patternsize = ret;
	return 0;
}

static int video_prepare_capture(struct device *dev, int nbufs, unsigned int offset,
				 const char *filename, enum buffer_fill_mode fill)
{
	unsigned int padding;
	unsigned int i;
	int ret;

	/* Allocate and map buffers. */
	padding = (fill & BUFFER_FILL_PADDING) ? 4096 : 0;
	if ((ret = video_alloc_buffers(dev, nbufs, offset, padding)) < 0)
		return ret;

	if (dev->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		ret = video_load_test_pattern(dev, filename);
		if (ret < 0)
			return ret;
	}

	/* Queue the buffers. */
	for (i = 0; i < dev->nbufs; ++i) {
		ret = video_queue_buffer(dev, i, fill);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void video_verify_buffer(struct device *dev, int index)
{
	struct buffer *buffer = &dev->buffers[index];
	const uint8_t *data = buffer->mem + buffer->size;
	unsigned int errors = 0;
	unsigned int dirty = 0;
	unsigned int i;

	if (buffer->padding == 0)
		return;

	for (i = 0; i < buffer->padding; ++i) {
		if (data[i] != 0x55) {
			errors++;
			dirty = i + 1;
		}
	}

	if (errors) {
		printf("Warning: %u bytes overwritten among %u first padding bytes\n",
		       errors, dirty);

		dirty = (dirty + 15) & ~15;
		dirty = dirty > 32 ? 32 : dirty;

		for (i = 0; i < dirty; ++i) {
			printf("%02x ", data[i]);
			if (i % 16 == 15)
				printf("\n");
		}
	}
}

static void video_save_image(struct device *dev, struct v4l2_buffer *buf,
			     const char *pattern, unsigned int sequence)
{
	unsigned int size;
	char *filename;
	const char *p;
	bool append;
	int ret;
	int fd;

	size = strlen(pattern);
	filename = malloc(size + 12);
	if (filename == NULL)
		return;

	p = strchr(pattern, '#');
	if (p != NULL) {
		sprintf(filename, "%.*s%06u%s", (int)(p - pattern), pattern,
			sequence, p + 1);
		append = false;
	} else {
		strcpy(filename, pattern);
		append = true;
	}

	fd = open(filename, O_CREAT | O_WRONLY | (append ? O_APPEND : 0),
		  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	free(filename);
	if (fd == -1)
		return;

	ret = write(fd, dev->buffers[buf->index].mem, buf->bytesused);
	close(fd);

	if (ret < 0)
		printf("write error: %s (%d)\n", strerror(errno), errno);
	else if (ret != (int)buf->bytesused)
		printf("write error: only %d bytes written instead of %u\n",
		       ret, buf->bytesused);
}

static int video_do_capture(struct device *dev, unsigned int nframes,
	unsigned int skip, unsigned int delay, const char *pattern,
	int do_requeue_last, enum buffer_fill_mode fill)
{
	struct timespec start;
	struct timeval last;
	struct timespec ts;
	struct v4l2_buffer buf;
	unsigned int size;
	unsigned int i;
	double bps;
	double fps;
	int ret;

	/* Start streaming. */
	ret = video_enable(dev, 1);
	if (ret < 0)
		goto done;

	size = 0;
	clock_gettime(CLOCK_MONOTONIC, &start);
	last.tv_sec = start.tv_sec;
	last.tv_usec = start.tv_nsec / 1000;

	for (i = 0; i < nframes; ++i) {
		/* Dequeue a buffer. */
		memset(&buf, 0, sizeof buf);
		buf.type = dev->type;
		buf.memory = dev->memtype;
		ret = ioctl(dev->fd, VIDIOC_DQBUF, &buf);
		if (ret < 0) {
			if (errno != EIO) {
				printf("Unable to dequeue buffer: %s (%d).\n",
					strerror(errno), errno);
				goto done;
			}
			buf.type = dev->type;
			buf.memory = dev->memtype;
			if (dev->memtype == V4L2_MEMORY_USERPTR)
				buf.m.userptr = (unsigned long)dev->buffers[i].mem;
		}

		if (dev->type == V4L2_BUF_TYPE_VIDEO_CAPTURE &&
		    dev->imagesize != 0	&& buf.bytesused != dev->imagesize)
			printf("Warning: bytes used %u != image size %u\n",
			       buf.bytesused, dev->imagesize);

		if (dev->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
			video_verify_buffer(dev, buf.index);

		size += buf.bytesused;

		fps = (buf.timestamp.tv_sec - last.tv_sec) * 1000000
		    + buf.timestamp.tv_usec - last.tv_usec;
		fps = fps ? 1000000.0 / fps : 0.0;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		printf("%u (%u) [%c] %u %u bytes %ld.%06ld %ld.%06ld %.3f fps\n", i, buf.index,
			(buf.flags & V4L2_BUF_FLAG_ERROR) ? 'E' : '-',
			buf.sequence, buf.bytesused, buf.timestamp.tv_sec,
			buf.timestamp.tv_usec, ts.tv_sec, ts.tv_nsec/1000, fps);

		last = buf.timestamp;

		/* Save the image. */
		if (dev->type == V4L2_BUF_TYPE_VIDEO_CAPTURE && pattern && !skip)
			video_save_image(dev, &buf, pattern, i);

		if (skip)
			--skip;

		/* Requeue the buffer. */
		if (delay > 0)
			usleep(delay * 1000);

		fflush(stdout);

		if (i == nframes - dev->nbufs && !do_requeue_last)
			continue;

		ret = video_queue_buffer(dev, buf.index, fill);
		if (ret < 0) {
			printf("Unable to requeue buffer: %s (%d).\n",
				strerror(errno), errno);
			goto done;
		}
	}

	/* Stop streaming. */
	video_enable(dev, 0);

	if (nframes == 0) {
		printf("No frames captured.\n");
		goto done;
	}

	if (ts.tv_sec == start.tv_sec && ts.tv_nsec == start.tv_nsec)
		goto done;

	ts.tv_sec -= start.tv_sec;
	ts.tv_nsec -= start.tv_nsec;
	if (ts.tv_nsec < 0) {
		ts.tv_sec--;
		ts.tv_nsec += 1000000000;
	}

	bps = size/(ts.tv_nsec/1000.0+1000000.0*ts.tv_sec)*1000000.0;
	fps = i/(ts.tv_nsec/1000.0+1000000.0*ts.tv_sec)*1000000.0;

	printf("Captured %u frames in %lu.%06lu seconds (%f fps, %f B/s).\n",
		i, ts.tv_sec, ts.tv_nsec/1000, fps, bps);

done:
	return video_free_buffers(dev);
}

#define V4L_BUFFERS_DEFAULT	8
#define V4L_BUFFERS_MAX		32

static void usage(const char *argv0)
{
	printf("Usage: %s [options] device\n", argv0);
	printf("Supported options:\n");
	printf("-c, --capture[=nframes]		Capture frames\n");
	printf("-C, --check-overrun		Verify dequeued frames for buffer overrun\n");
	printf("-d, --delay			Delay (in ms) before requeuing buffers\n");
	printf("-f, --format format		Set the video format\n");
	printf("-F, --file[=name]		Read/write frames from/to disk\n");
	printf("\tFor video capture devices, the first '#' character in the file name is\n");
	printf("\texpanded to the frame sequence number. The default file name is\n");
	printf("\t'frame-#.bin'.\n");
	printf("-h, --help			Show this help screen\n");
	printf("-i, --input input		Select the video input\n");
	printf("-I, --fill-frames		Fill frames with check pattern before queuing them\n");
	printf("-l, --list-controls		List available controls\n");
	printf("-n, --nbufs n			Set the number of video buffers\n");
	printf("-p, --pause			Pause before starting the video stream\n");
	printf("-q, --quality n			MJPEG quality (0-100)\n");
	printf("-r, --get-control ctrl		Get control 'ctrl'\n");
	printf("-R, --realtime=[priority]	Enable realtime RR scheduling\n");
	printf("-s, --size WxH			Set the frame size\n");
	printf("-t, --time-per-frame num/denom	Set the time per frame (eg. 1/25 = 25 fps)\n");
	printf("-u, --userptr			Use the user pointers streaming method\n");
	printf("-w, --set-control 'ctrl value'	Set control 'ctrl' to 'value'\n");
	printf("    --enum-formats		Enumerate formats\n");
	printf("    --enum-inputs		Enumerate inputs\n");
	printf("    --no-query			Don't query capabilities on open\n");
	printf("    --offset			User pointer buffer offset from page start\n");
	printf("    --requeue-last		Requeue the last buffers before streamoff\n");
	printf("    --skip n			Skip the first n frames\n");
	printf("    --sleep-forever		Sleep forever after configuring the device\n");
	printf("    --stride value		Line stride in bytes\n");
}

#define OPT_ENUM_FORMATS	256
#define OPT_ENUM_INPUTS		257
#define OPT_SKIP_FRAMES		258
#define OPT_NO_QUERY		259
#define OPT_SLEEP_FOREVER	260
#define OPT_USERPTR_OFFSET	261
#define OPT_REQUEUE_LAST	262
#define OPT_STRIDE		263

static struct option opts[] = {
	{"capture", 2, 0, 'c'},
	{"check-overrun", 0, 0, 'C'},
	{"delay", 1, 0, 'd'},
	{"enum-formats", 0, 0, OPT_ENUM_FORMATS},
	{"enum-inputs", 0, 0, OPT_ENUM_INPUTS},
	{"file", 2, 0, 'F'},
	{"fill-frames", 0, 0, 'I'},
	{"format", 1, 0, 'f'},
	{"help", 0, 0, 'h'},
	{"input", 1, 0, 'i'},
	{"list-controls", 0, 0, 'l'},
	{"nbufs", 1, 0, 'n'},
	{"no-query", 0, 0, OPT_NO_QUERY},
	{"offset", 1, 0, OPT_USERPTR_OFFSET},
	{"pause", 0, 0, 'p'},
	{"quality", 1, 0, 'q'},
	{"get-control", 1, 0, 'r'},
	{"requeue-last", 0, 0, OPT_REQUEUE_LAST},
	{"realtime", 2, 0, 'R'},
	{"size", 1, 0, 's'},
	{"set-control", 1, 0, 'w'},
	{"skip", 1, 0, OPT_SKIP_FRAMES},
	{"sleep-forever", 0, 0, OPT_SLEEP_FOREVER},
	{"stride", 1, 0, OPT_STRIDE},
	{"time-per-frame", 1, 0, 't'},
	{"userptr", 0, 0, 'u'},
	{0, 0, 0, 0}
};

int main(int argc, char *argv[])
{
	struct sched_param sched;
	struct device dev;
	int ret;

	/* Options parsings */
	int do_file = 0, do_capture = 0, do_pause = 0;
	int do_set_time_per_frame = 0;
	int do_enum_formats = 0, do_set_format = 0;
	int do_enum_inputs = 0, do_set_input = 0;
	int do_list_controls = 0, do_get_control = 0, do_set_control = 0;
	int do_sleep_forever = 0, do_requeue_last = 0;
	int do_rt = 0;
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
	unsigned int stride = 0;
	unsigned int nbufs = V4L_BUFFERS_DEFAULT;
	unsigned int input = 0;
	unsigned int skip = 0;
	unsigned int quality = (unsigned int)-1;
	unsigned int userptr_offset = 0;
	struct v4l2_fract time_per_frame = {1, 25};

	/* Capture loop */
	enum buffer_fill_mode fill_mode = BUFFER_FILL_NONE;
	unsigned int delay = 0, nframes = (unsigned int)-1;
	const char *filename = "frame-#.bin";

	unsigned int rt_priority = 1;

	opterr = 0;
	while ((c = getopt_long(argc, argv, "c::Cd:f:F::hi:Iln:pq:r:R::s:t:uw:", opts, NULL)) != -1) {

		switch (c) {
		case 'c':
			do_capture = 1;
			if (optarg)
				nframes = atoi(optarg);
			break;
		case 'C':
			fill_mode |= BUFFER_FILL_PADDING;
			break;
		case 'd':
			delay = atoi(optarg);
			break;
		case 'f':
			do_set_format = 1;
			pixelformat = v4l2_format_code(optarg);
			if (pixelformat == 0) {
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
		case 'I':
			fill_mode |= BUFFER_FILL_FRAME;
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
		case 'R':
			do_rt = 1;
			if (optarg)
				rt_priority = atoi(optarg);
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
		case OPT_REQUEUE_LAST:
			do_requeue_last = 1;
			break;
		case OPT_SKIP_FRAMES:
			skip = atoi(optarg);
			break;
		case OPT_SLEEP_FOREVER:
			do_sleep_forever = 1;
			break;
		case OPT_STRIDE:
			stride = atoi(optarg);
			break;
		case OPT_USERPTR_OFFSET:
			userptr_offset = atoi(optarg);
			break;
		default:
			printf("Invalid option -%c\n", c);
			printf("Run %s -h for help.\n", argv[0]);
			return 1;
		}
	}

	if ((fill_mode & BUFFER_FILL_PADDING) && memtype != V4L2_MEMORY_USERPTR) {
		printf("Buffer overrun can only be checked in USERPTR mode.\n");
		return 1;
	}

	if (optind >= argc) {
		usage(argv[0]);
		return 1;
	}

	if (!do_file)
		filename = NULL;

	/* Open the video device. If the device type isn't recognized, set the
	 * --no-query option to avoid querying V4L2 subdevs.
	 */
	ret = video_open(&dev, argv[optind], no_query);
	if (ret < 0)
		return 1;

	if (dev.type == (enum v4l2_buf_type)-1)
		no_query = 1;

	dev.memtype = memtype;

	if (do_get_control) {
		int64_t val;
		ret = get_control(&dev, ctrl_name,
				  get_control_type(&dev, ctrl_name), &val);
		if (ret >= 0)
			printf("Control 0x%08x value %" PRId64 "\n", ctrl_name, val);
	}

	if (do_set_control)
		set_control(&dev, ctrl_name, get_control_type(&dev, ctrl_name),
			    ctrl_value);

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
		if (video_set_format(&dev, width, height, pixelformat, stride) < 0) {
			video_close(&dev);
			return 1;
		}
	}

	if (!no_query || do_capture)
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

	if (video_prepare_capture(&dev, nbufs, userptr_offset, filename, fill_mode)) {
		video_close(&dev);
		return 1;
	}

	if (do_pause) {
		printf("Press enter to start capture\n");
		getchar();
	}

	if (do_rt) {
		memset(&sched, 0, sizeof sched);
		sched.sched_priority = rt_priority;
		ret = sched_setscheduler(0, SCHED_RR, &sched);
		if (ret < 0)
			printf("Failed to select RR scheduler: %s (%d)\n",
				strerror(errno), errno);
	}

	if (video_do_capture(&dev, nframes, skip, delay, filename,
			     do_requeue_last, fill_mode) < 0) {
		video_close(&dev);
		return 1;
	}

	video_close(&dev);
	return 0;
}

