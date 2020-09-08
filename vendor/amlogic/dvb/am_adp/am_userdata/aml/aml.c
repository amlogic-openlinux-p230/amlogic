#ifdef _FORTIFY_SOURCE
#undef _FORTIFY_SOURCE
#endif
/***************************************************************************
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Description:
 */
/**\file
 * \brief aml user data driver
 *
 * \author Xia Lei Peng <leipeng.xia@amlogic.com>
 * \date 2013-3-13: create the document
 ***************************************************************************/

#define AM_DEBUG_LEVEL 1

#include <am_debug.h>
#include <am_mem.h>
#include "../am_userdata_internal.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <sys/ioctl.h>

#define USERDATA_POLL_TIMEOUT 100
#define MAX_CC_NUM			64

#define IS_H264(p)	((p[0] == 0xb5 && p[3] == 0x47 && p[4] == 0x41 && p[5] == 0x39 && p[6] == 0x34))
#define IS_DIRECTV(p) ((p[0] == 0xb5 && p[1] == 0x00 && p[2] == 0x2f))
#define IS_AVS(p)	 ((p[0] == 0x47) && (p[1] == 0x41) && (p[2] == 0x39) && (p[3] == 0x34))
#define IS_ATSC(p)	((p[0] == 0x47) && (p[1] == 0x41) && (p[2] == 0x39) && (p[3] == 0x34))

#define AMSTREAM_IOC_MAGIC  'S'
#define AMSTREAM_IOC_UD_LENGTH _IOR(AMSTREAM_IOC_MAGIC, 0x54, unsigned long)
#define AMSTREAM_IOC_UD_POC _IOR(AMSTREAM_IOC_MAGIC, 0x55, int)
#define AMSTREAM_IOC_UD_FLUSH_USERDATA _IOR(AMSTREAM_IOC_MAGIC, 0x56, int)

/****************************************************************************
 * Type definitions
 ***************************************************************************/

typedef enum {
	INVALID_TYPE 	= 0,
	MPEG_CC_TYPE 	= 1,
	H264_CC_TYPE 	= 2,
	DIRECTV_CC_TYPE = 3,
	AVS_CC_TYPE 	= 4
} userdata_type;


typedef enum {
	/* 0 forbidden */
	I_TYPE = 1,
	P_TYPE = 2,
	B_TYPE = 3,
	D_TYPE = 4,
	/* 5 ... 7 reserved */
} picture_coding_type;

typedef struct {
	 unsigned int poc_info;
	 unsigned int poc_number;
} userdata_poc_info_t;

typedef struct AM_CCData_s AM_CCData;
struct AM_CCData_s {
	AM_CCData *next;
	uint8_t   *buf;
	int		size;
	int		cap;
	int		poc;
};

typedef struct {
	uint32_t picture_structure:16;
	uint32_t temporal_reference:10;
	uint32_t picture_coding_type:3;
	uint32_t reserved:3;
	uint32_t index:16;
	uint32_t offset:16;
	uint8_t  atsc_flag[4];
	uint8_t  cc_data_start[4];
} aml_ud_header_t;

typedef struct {
	pthread_t	   th;
	int			 fd;
	AM_Bool_t	   running;
	AM_CCData	  *cc_list;
	AM_CCData	  *free_list;
	int			 cc_num;
	userdata_type   format;
	int			 curr_poc;
} AM_UDDrvData;


/****************************************************************************
 * Static data definitions
 ***************************************************************************/

static AM_ErrorCode_t aml_open(AM_USERDATA_Device_t *dev, const AM_USERDATA_OpenPara_t *para);
static AM_ErrorCode_t aml_close(AM_USERDATA_Device_t *dev);

const AM_USERDATA_Driver_t aml_ud_drv = {
.open  = aml_open,
.close = aml_close,
};

static void aml_free_cc_data (AM_CCData *cc)
{
	if (cc->buf)
		free(cc->buf);
	free(cc);
}

static void aml_swap_data(uint8_t *user_data, int ud_size)
{
	int swap_blocks, i, j, k, m;
	unsigned char c_temp;

	/* swap byte order */
	swap_blocks = ud_size >> 3;
	for (i = 0; i < swap_blocks; i ++) {
		j = i << 3;
		k = j + 7;
		for (m=0; m<4; m++) {
			c_temp = user_data[j];
			user_data[j++] = user_data[k];
			user_data[k--] = c_temp;
		}
	}
}

static userdata_type aml_check_userdata_format (uint8_t *buf, int len)
{
	if (len < 8)
		return INVALID_TYPE;

	if (IS_H264(buf)) {
		AM_DEBUG(1,"CC format is h264_cc_type");
		return H264_CC_TYPE;
	} else if (IS_DIRECTV(buf)) {
		AM_DEBUG(1,"CC format is directv_cc_type");
		return DIRECTV_CC_TYPE;
	} else if (IS_AVS(buf)) {
		AM_DEBUG(1,"CC format is avs_cc_type");
		return AVS_CC_TYPE;
	} else if (len >= (int)sizeof(aml_ud_header_t)) {
		aml_ud_header_t *hdr = (aml_ud_header_t*)buf;

		if (IS_ATSC(hdr->atsc_flag)) {
			AM_DEBUG(1,"CC format is mpeg_cc_type");
			return MPEG_CC_TYPE;
		}
	}

	return INVALID_TYPE;
}

static void aml_flush_cc_data(AM_USERDATA_Device_t *dev)
{
	AM_UDDrvData *ud = dev->drv_data;
	AM_CCData *cc, *ncc;
	char  buf[256];
	char *pp = buf;
	int   left = sizeof(buf), i, pr;

	for (cc = ud->cc_list; cc; cc = ncc) {
		ncc = cc->next;

		for (i = 0; i < cc->size; i ++) {
			pr = snprintf(pp, left, "%02x ", cc->buf[i]);
			if (pr < left) {
				pp   += pr;
				left -= pr;
			}
		}

		AM_DEBUG(1, "CC decode:%s", buf);

		dev->write_package(dev, cc->buf, cc->size);

		cc->next = ud->free_list;
		ud->free_list = cc;
	}

	ud->cc_list  = NULL;
	ud->cc_num   = 0;
	ud->curr_poc = -1;
}

static void aml_add_cc_data(AM_USERDATA_Device_t *dev, int poc, int type, uint8_t *p, int len)
{
	AM_UDDrvData *ud = dev->drv_data;
	AM_CCData **pcc, *cc;
	char  buf[256];
	char *pp = buf;
	int   left = sizeof(buf), i, pr;

	if (ud->cc_num >= MAX_CC_NUM) {
		aml_flush_cc_data(dev);
	}

	for (i = 0; i < len; i ++) {
		pr = snprintf(pp, left, "%02x ", p[i]);
		if (pr < left) {
			pp   += pr;
			left -= pr;
		}
	}

	AM_DEBUG(1, "CC poc:%d ptype:%d data:%s", poc, type, buf);

	pcc = &ud->cc_list;

	while ((cc = *pcc)) {
		/*if (cc->poc == poc) {
			aml_flush_cc_data(dev);
			pcc = &ud->cc_list;
			break;
		}*/

		if (cc->poc > poc) {
			break;
		}

		pcc = &cc->next;
	}

	if (ud->free_list) {
		cc = ud->free_list;
		ud->free_list = cc->next;
	} else {
		cc = malloc(sizeof(AM_CCData));
		cc->buf  = NULL;
		cc->size = 0;
		cc->cap  = 0;
		cc->poc  = 0;
	}

	if (cc->cap < len) {
		cc->buf = realloc(cc->buf, len);
		cc->cap = len;
	}

	memcpy(cc->buf, p, len);

	cc->size = len;
	cc->poc  = poc;
	cc->next = *pcc;
	*pcc = cc;

	ud->cc_num ++;
}

static void aml_mpeg_userdata_package(AM_USERDATA_Device_t *dev, int poc, int type, uint8_t *p, int len)
{
	AM_UDDrvData *ud = dev->drv_data;

	if (len < 5)
		return;

	if (p[4] != 3)
		return;

	if (type == I_TYPE)
		aml_flush_cc_data(dev);

	if (poc == ud->curr_poc + 1) {
		AM_CCData *cc, **pcc;

		dev->write_package(dev, p, len);
		ud->curr_poc ++;

		pcc = &ud->cc_list;
		while ((cc = *pcc)) {
			if (ud->curr_poc + 1 != cc->poc)
				break;

			dev->write_package(dev, cc->buf, cc->size);
			*pcc = cc->next;
			ud->curr_poc ++;

			cc->next = ud->free_list;
			ud->free_list = cc;
		}

		return;
	}

	aml_add_cc_data(dev, poc, type, p, len);
}

static int aml_process_mpeg_userdata(AM_USERDATA_Device_t *dev, uint8_t *data, int len)
{
	AM_UDDrvData *ud = dev->drv_data;
	uint8_t *pd = data;
	int left = len;
	int r = 0;

	while (left >= (int)sizeof(aml_ud_header_t)) {
		aml_ud_header_t *hdr = (aml_ud_header_t*)pd;
		int ref, ptype;

		if (IS_ATSC(hdr->atsc_flag)) {
			aml_ud_header_t *nhdr;
			uint8_t *pp, t;
			uint32_t v;
			int pl;

			pp = (uint8_t*)&hdr->atsc_flag;
			pl = 8;

			v = (pd[4] << 24) | (pd[5] << 16) | (pd[6] << 8) | pd[7];
			ref   = (v >> 16) & 0x3f;
			ptype = (v >> 26) & 7;

			/*AM_DEBUG(0, "CC header: %02x %02x %02x %02x %02x %02x %02x %02x",
				pd[0], pd[1], pd[2], pd[3], pd[4], pd[5], pd[6], pd[7]);
				*/
			pd   += sizeof(aml_ud_header_t);
			left -= sizeof(aml_ud_header_t);

			while (left >= 8) {
				if (left >= (int)sizeof(aml_ud_header_t)) {
					nhdr = (aml_ud_header_t*)pd;
					if (IS_ATSC(nhdr->atsc_flag)) {
						aml_mpeg_userdata_package(dev, ref, ptype, pp, pl);

						r += pl + 8;
						break;
					} else {
						pd   += 8;
						pl   += 8;
						left -= 8;
					}
				} else {
					break;
				}
			}
		} else {
			pd   += 8;
			left -= 8;
			r	+= 8;
		}
	}

	return r;
}

static int aml_process_h264_userdata(AM_USERDATA_Device_t *dev, uint8_t *data, int len)
{
	AM_UDDrvData *ud = dev->drv_data;
	int fd = ud->fd;
	uint8_t *pd = data;
	int left = len;
	int r = 0;

	while (left >= 8) {
		if (IS_H264(pd) || IS_DIRECTV(pd) || IS_AVS(pd)) {
			int hdr = (ud->format == H264_CC_TYPE) ? 3 : 0;
			int pl;
			userdata_poc_info_t poc_block;
			int r;

			pd += hdr;

			pl = 8 + (pd[5] & 0x1f) * 3;

			if (pl + hdr > left)
				break;

			memset(&poc_block, 0, sizeof(userdata_poc_info_t));
			ioctl(fd, AMSTREAM_IOC_UD_POC, &poc_block);

			if (poc_block.poc_number == 0) {
				aml_flush_cc_data(dev);
			}

			aml_add_cc_data(dev, poc_block.poc_number, I_TYPE, pd, pl);

			pd   += pl;
			left -= pl + hdr;
			r	+= pl + hdr;
		} else {
			pd   += 8;
			left -= 8;
			r	+= 8;
		}
	}

	return r;
}

static void* aml_userdata_thread (void *arg)
{
	AM_USERDATA_Device_t *dev = (AM_USERDATA_Device_t*)arg;
	AM_UDDrvData *ud = dev->drv_data;
	int fd = ud->fd;
	int r;
	struct pollfd pfd;
	uint8_t data[5*1024];
	uint8_t *pd = data;
	int left = 0;
	int flush = 1;

	pfd.events = POLLIN|POLLERR;
	pfd.fd	 = fd;

	while (ud->running) {
		r = poll(&pfd, 1, USERDATA_POLL_TIMEOUT);
		if (r != 1)
			continue;
		if (!(pfd.revents & POLLIN))
			continue;

		r = read(fd, data + left, sizeof(data) - left);
		if (flush) {
			ioctl(fd, AMSTREAM_IOC_UD_FLUSH_USERDATA, NULL);
			flush = 0;
			continue;
		}

		if (r <= 0)
			continue;

		aml_swap_data(data + left, r);

		left += r;
		pd = data;

		while (ud->format == INVALID_TYPE) {
			if (left < 8)
				break;

			ud->format = aml_check_userdata_format(pd, left);
			if (ud->format == INVALID_TYPE) {
				pd   += 8;
				left -= 8;
			}
		}

		if (ud->format == MPEG_CC_TYPE) {
			r = aml_process_mpeg_userdata(dev, pd, left);
		} else if (ud->format != INVALID_TYPE) {
			r = aml_process_h264_userdata(dev, pd, left);
		} else {
			r = 0;
		}

		if ((data != pd + r) && (r < left)) {
			memmove(data, pd + r, left - r);
		}

		left -= r;
	}

	return NULL;
}

static AM_ErrorCode_t aml_open(AM_USERDATA_Device_t *dev, const AM_USERDATA_OpenPara_t *para)
{
	AM_UDDrvData *ud = malloc(sizeof(AM_UDDrvData));
	int r;

	if (!ud) {
		AM_DEBUG(0, "not enough memory");
		return AM_USERDATA_ERR_NO_MEM;
	}

	ud->fd = open("/dev/amstream_userdata", O_RDONLY);
	if (ud->fd == -1) {
		AM_DEBUG(0, "cannot open userdata device");
		free(ud);
		return AM_USERDATA_ERR_CANNOT_OPEN_DEV;
	}

	ud->format	= INVALID_TYPE;
	ud->cc_list   = NULL;
	ud->free_list = NULL;
	ud->running   = AM_TRUE;
	ud->cc_num	= 0;
	ud->curr_poc  = -1;

	r = pthread_create(&ud->th, NULL, aml_userdata_thread, (void*)dev);
	if (r) {
		AM_DEBUG(0, "create userdata thread failed");
		close(ud->fd);
		free(ud);
		return AM_USERDATA_ERR_SYS;
	}

	dev->drv_data = ud;
	return AM_SUCCESS;
}

static AM_ErrorCode_t aml_close(AM_USERDATA_Device_t *dev)
{
	AM_UDDrvData *ud = dev->drv_data;
	AM_CCData *cc, *cc_next;

	ud->running = AM_FALSE;
	pthread_join(ud->th, NULL);
	close(ud->fd);

	for (cc = ud->cc_list; cc; cc = cc_next) {
		cc_next = cc->next;
		aml_free_cc_data(cc);
	}

	for (cc = ud->free_list; cc; cc = cc_next) {
		cc_next = cc->next;
		aml_free_cc_data(cc);
	}


	free (ud);
	return AM_SUCCESS;
}

