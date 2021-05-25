/*
* # SPDX-License-Identifier: GPL-2.0-or-later
* # Copyright (C) 2018-present Team CoreELEC (https://coreelec.org)
*/

#include <stdio.h>
#include <stdlib.h>
#include <linux/fb.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <time.h>
#include <byteswap.h>
#include <spng.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>

#ifndef __SPLASH_IMAGE_H__
#define __SPLASH_IMAGE_H__

#define FRAMEBUFFER_DEVICE  "/dev/fb0"

#define BLEND(back, front, alpha) ((back * (1.0 - alpha)) + (front * alpha))
#define APPLY_ALPHA(back, front, alpha) ( \
  ((uint8_t)(BLEND(((back >>  16) & 0xFF), ((front >>  16) & 0xFF), alpha)) <<  16) | \
  ((uint8_t)(BLEND(((back >>   8) & 0xFF), ((front >>   8) & 0xFF), alpha)) <<   8) | \
  ((uint8_t)(BLEND((back & 0xFF), (front & 0xFF), alpha)) & 0xFF))

typedef struct _scale
{
  uint8_t resize;
  float x;
  float y;
  uint8_t bytes_per_pixel;
} scale_t;

typedef struct _offset
{
  uint32_t x;
  uint32_t y;
} offset_t;

typedef struct _files
{
  uint32_t count;
  uint32_t current;
  unsigned char **data;
  size_t **size;
} files_t;

typedef struct _animation_data
{
  struct fb_var_screeninfo *vinfo;
  struct fb_fix_screeninfo *finfo;
  uint8_t *fbp;
  files_t *files;
  scale_t *scale;
  uint8_t *out_full_image;
  uint8_t **out_full_image_animation;
  uint8_t **out_image;
  offset_t *offset;
} animation_data_t;

#endif
