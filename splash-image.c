/*
* # SPDX-License-Identifier: GPL-2.0-or-later
* # Copyright (C) 2020-present Team CoreELEC (https://coreelec.org)
*/

#include "splash-image.h"
#include "splash-timer.h"

uint32_t screen_size;
int fmt = SPNG_FMT_BGRA8;
uint8_t end_animation = 0;

int get_resize_factor(struct fb_var_screeninfo vinfo, struct spng_ihdr ihdr, scale_t *scale)
{
	if (vinfo.xres == ihdr.width && vinfo.yres == ihdr.height)
		return 0;

	scale->x = ((float) ihdr.width)  / ((float) vinfo.xres);
	scale->y = ((float) ihdr.height) / ((float) vinfo.yres);

	scale->bytes_per_pixel = vinfo.bits_per_pixel / 8;

	return 1;
}

int do_resize(unsigned char **out, struct spng_ihdr *ihdr, scale_t scale)
{
	uint32_t x, y, old_x, old_y;
	unsigned char *new_out;
	uint32_t new_x = ceil(ihdr->width  / scale.x);
	uint32_t new_y = ceil(ihdr->height / scale.y);
	uint32_t new_size = new_x * new_y * scale.bytes_per_pixel;

	new_out = malloc(new_size);

	for (y = 0; y < new_y; y++)
	{
		old_y = y * scale.y;
		for (x = 0; x < new_x; x++)
		{
			old_x = x * scale.x;
			*(uint32_t *)(new_out + x * scale.bytes_per_pixel + y * new_x * scale.bytes_per_pixel) =
				*(uint32_t *)(*out + old_x * scale.bytes_per_pixel + old_y * ihdr->width * scale.bytes_per_pixel);
		}
	}

	*out = realloc(*out, new_size);
	memcpy(*out, new_out, new_size);
	ihdr->width  = new_x;
	ihdr->height = new_y;
	free(new_out);

	return new_size;
}

void full_alpha_transparency(uint32_t *full_pixel, uint32_t *alpha_pixel)
{
	double alpha = (*alpha_pixel >> 24) / 255.;

	if (alpha > 0.0)
		*full_pixel = 0xFF000000 | (uint32_t)APPLY_ALPHA((*full_pixel), (*alpha_pixel), alpha);
}

int open_framebuffer(char *fb_dev, struct fb_var_screeninfo *vinfo, struct fb_fix_screeninfo *finfo)
{
	int fd = open(fb_dev, O_RDWR);

	if (fd < 0)
		return fd;

	// get vscreeninfo
	if (ioctl(fd, FBIOGET_VSCREENINFO, vinfo))
	{
		printf("Error calling ioctl 'FBIOGET_VSCREENINFO'\n");
		close(fd);
		return -1;
	}

	// get fscreeninfo
	if (ioctl(fd, FBIOGET_FSCREENINFO, finfo))
	{
		printf("Error calling ioctl 'FBIOGET_FSCREENINFO'\n");
		close(fd);
		return -1;
	}

	// adjust vinfo
	vinfo->grayscale = 0;
	vinfo->bits_per_pixel = 32;

	// put vscreeninfo
	if (ioctl(fd, FBIOPUT_VSCREENINFO, vinfo))
	{
		printf("Error calling ioctl 'FBIOPUT_VSCREENINFO'\n");
		close(fd);
		return -1;
	}

	// refresh vscreeninfo
	if (ioctl(fd, FBIOGET_VSCREENINFO, vinfo))
	{
		printf("Error calling ioctl 'FBIOGET_VSCREENINFO'\n");
		close(fd);
		return -1;
	}

	return fd;
}

size_t decode_image(spng_ctx *ctx, uint8_t **out, size_t old_out_size, int fmt, struct spng_ihdr *ihdr)
{
	int r;
	size_t out_size;
	r = spng_get_ihdr(ctx, ihdr);
	if(r)
	{
		printf("spng_get_ihdr() error: %s\n", spng_strerror(r));
		return 0;
	}

	// get decoded image size
	r = spng_decoded_image_size(ctx, fmt, &out_size);
	if(r)
	{
		printf("spng_decoded_image_size() error: %s\n", spng_strerror(r));
		return 0;
	}

	// create memory buffer for decoded image
	if (old_out_size != out_size)
	{
		if (*out != NULL)
			free(*out);

		*out = malloc(out_size);
		if(*out == NULL)
		{
			printf("Failed to allocating memory for decode buffer\n");
			return 0;
		}
	}

	// decode image
	r = spng_decode_image(ctx, *out, out_size, fmt, 0);
	if(r)
	{
		printf("spng_decode_image() error: %s\n", spng_strerror(r));
		return 0;
	}

	return out_size;
}

spng_ctx *create_spng_context(uint8_t *file, size_t files_size, uint32_t screen_size)
{
	spng_ctx *ctx = spng_ctx_new(0);
	if(ctx == NULL)
	{
		printf("spng_ctx_new() failed\n");
		return NULL;
	}

	// Ignore and don't calculate chunk CRC's
	spng_set_crc_action(ctx, SPNG_CRC_USE, SPNG_CRC_USE);

	// Set memory usage limits for storing standard and unknown chunks,
	// this is important when reading arbitrary files!
	size_t limit = screen_size * 8;
	spng_set_chunk_limits(ctx, limit, limit);

	// Set source PNG
	spng_set_png_buffer(ctx, file, files_size);

	return ctx;
}

int file_exists(char *file)
{
  struct stat sb;
  return (stat(file, &sb) == 0);
}

void show_next_image(size_t timer_id, void * user_data)
{
  animation_data_t *animation_data = (animation_data_t *)user_data;
	struct fb_var_screeninfo *vinfo = (struct fb_var_screeninfo *)animation_data->vinfo;
	struct fb_fix_screeninfo *finfo = (struct fb_fix_screeninfo *)animation_data->finfo;
	uint8_t *fbp = (uint8_t *)animation_data->fbp;
	uint8_t *out_full_image = (uint8_t *)animation_data->out_full_image;
	uint8_t **out_full_image_animation = (uint8_t **)animation_data->out_full_image_animation;
	uint8_t **out_image = (uint8_t **)animation_data->out_image;
	scale_t *scale = (scale_t *)animation_data->scale;
	offset_t *offset = (offset_t *)animation_data->offset;
	files_t *files = (files_t *)animation_data->files;
	size_t out_size = 0;
	spng_ctx *ctx = NULL;
	struct spng_ihdr ihdr;
	uint32_t x,y;

	// create buffer for blended image
	if (*out_full_image_animation == NULL)
	{
		*out_full_image_animation = malloc(screen_size);
		if(*out_full_image_animation == NULL)
		{
			printf("Failed to create backup of full screen image\n");
			end_animation = 1;
			return;
		}
	}

	// restore full background image
	memcpy(*out_full_image_animation, out_full_image, screen_size);

	// Create a context for the full screen image
	ctx = create_spng_context(files->data[files->current], *files->size[files->current], screen_size);
	if(ctx == NULL)
	{
		printf("Failed to create a new spng context\n");
		end_animation = 1;
		return;
	}

	// decode image and store in buffer
	out_size = decode_image(ctx, out_image, out_size, fmt, &ihdr);
	if (!out_size)
	{
		printf("Failed to decode image\n");
		end_animation = 1;
		return;
	}

	// rescale image if needed
	if (scale->resize)
		out_size = do_resize(out_image, &ihdr, *scale);

	// blend alpha image over full image
	for (y = 0; y < ihdr.height; y++)
	{
		for (x = 0; x < ihdr.width; x++)
		{
			full_alpha_transparency((uint32_t *)(*out_full_image_animation + (x + offset->x) * vinfo->bits_per_pixel / 8
				+ (y + offset->y) * finfo->line_length),
				(uint32_t *)(*out_image + x * vinfo->bits_per_pixel / 8 + y * out_size / ihdr.height));
		}
	}

	// show blended image on framebuffer
	memcpy(fbp, *out_full_image_animation, screen_size);

	spng_ctx_free(ctx);

	// increase sounter for use next image
	files->current++;

	// restart loop with image 1, image 0 is the "full screen" image
	if (files->current == files->count)
		files->current = 1;
}

static void handle_signal(int sig)
{
	switch (sig)
	{
		case SIGUSR1:
			end_animation = 1;
			break;
	}
}

static void daemonize()
{
	pid_t process_id = 0;
	pid_t sid = 0;

	// Create child process
	process_id = fork();
	// Indication of fork() failure
	if (process_id < 0)
	{
		printf("fork failed!\n");
		// Return failure in exit status
		exit(1);
	}

	// PARENT PROCESS. Need to kill it.
	if (process_id > 0)
	{
		// return success in exit status
		exit(0);
	}

	//unmask the file mode
	umask(0);

	//set new session
	sid = setsid();
	if(sid < 0)
	{
		// Return failure
		exit(1);
	}
}

int main(int argc, char** argv)
{
	int i;
	int fbfd = 0;
	uint8_t *fbp = NULL;
	struct fb_var_screeninfo vinfo;
	struct fb_fix_screeninfo finfo;

	// spng
	spng_ctx *ctx_full_image = NULL;
	struct spng_ihdr ihdr;
	size_t out_size = 0;
	scale_t scale;
	offset_t offset;
	uint8_t framerate = 20;

	FILE *fd;
	files_t files = {0};
	uint8_t *out_full_image = NULL;

	// default splash path
	char *splash_path = "/splash/splash-1080.png";
	char animation_file_path[512];

	// add signal handler to be able to get stopped by SIGUSR1
	signal(SIGUSR1, handle_signal);

	// check if splash/progress path is given
	if (argc >= 2)
		splash_path = argv[1];

	// check if single splash is used
	if (file_exists(splash_path))
	{
		files.data = malloc(sizeof(uint8_t *));
		files.size = malloc(sizeof(size_t *));

		// read single splash to memory buffer
		fd = fopen(splash_path, "rb");
		if(fd == NULL)
		{
			printf("Error opening input file %s\n", splash_path);
			goto Exit;
		}

		fseek(fd, 0L, SEEK_END);
		files.size[0] = malloc(sizeof(size_t));
		*files.size[0] = ftell(fd);
		fseek(fd, 0L, SEEK_SET);
		files.data[0] = malloc(*files.size[0]);
		fread(files.data[0], *files.size[0], 1, fd);
		fclose(fd);

		files.count++;
	}
	else
	{
		char *line = NULL;
		char key[512];
		int value;
		uint8_t animation_en = 1;
		size_t len = 0;
		ssize_t read;

		// when reading animation from /splash daemonize to run in background
		if (strncmp(splash_path, "/splash", sizeof("/splash") - 1) == 0)
			daemonize();

		// check if animation config does exist
		snprintf(animation_file_path, sizeof(animation_file_path), "%sconfig", splash_path);
		if (!file_exists(animation_file_path))
		{
			printf("Error opening animation config file %s\n", animation_file_path);
			goto Exit;
		}

		// read animation config file
		fd = fopen(animation_file_path, "r");
		if(fd == NULL)
		{
			printf("Error opening animation config file %s\n", animation_file_path);
			goto Exit;
		}

		while ((read = getline(&line, &len, fd)) != -1) {
			sscanf(line, "%[^=]%*[^0123456789]%d", key, &value);

			if (!strcmp(key, "animation_enable"))
				animation_en = value;
			else if (!strcmp(key, "animation_offset_x"))
				offset.x = value;
			else if (!strcmp(key, "animation_offset_y"))
				offset.y = value;
			else if (!strcmp(key, "frames_per_second"))
				framerate = value;
		}

		fclose(fd);
		if (line)
			free(line);

		// read all animation images
		snprintf(animation_file_path, sizeof(animation_file_path),
			"%s%d.png", splash_path, files.count);

		// read all pngs to memory
		while (file_exists(animation_file_path))
		{
			files.data = realloc(files.data, (files.count + 1) * sizeof(uint8_t *));
			files.size = realloc(files.size, (files.count + 1) * sizeof(size_t *));

			fd = fopen(animation_file_path, "rb");
			if(fd == NULL)
			{
				printf("Error opening input file %s\n", animation_file_path);
				goto Exit;
			}

			fseek(fd, 0L, SEEK_END);
			files.size[files.count] = malloc(sizeof(size_t));
			*files.size[files.count] = ftell(fd);
			fseek(fd, 0L, SEEK_SET);
			files.data[files.count] = malloc(*files.size[files.count]);
			fread(files.data[files.count], *files.size[files.count], 1, fd);
			fclose(fd);

			files.count++;

			// stop searching more images when disabled
			if (!animation_en)
				break;

			// try next animation splash
			snprintf(animation_file_path, sizeof(animation_file_path),
				"%s%d.png", splash_path, files.count);
		}

		// when reading animation from /flash daemonize to run in background
		if (strncmp(splash_path, "/splash", sizeof("/splash") - 1) != 0)
			daemonize();
	}

	// check if any splash got found
	if (!files.count)
	{
		printf("No input file found: %s\n", splash_path);
		goto Exit;
	}

	// open framebuffer device
	fbfd = open_framebuffer(FRAMEBUFFER_DEVICE, &vinfo, &finfo);
	if (fbfd < 0)
	{
		printf("Error opening framebuffer device '%s'\n", FRAMEBUFFER_DEVICE);
		goto Exit;
	}

	screen_size = vinfo.yres * vinfo.xres * vinfo.bits_per_pixel / 8;

	// memory map device framebuffer
	fbp = mmap(0, vinfo.yres * finfo.line_length, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
	if (fbp == MAP_FAILED)
	{
		printf("Error mapping framebuffer memory\n");
		goto Exit;
	}

	// Create a context for the full screen image
	ctx_full_image = create_spng_context(files.data[0], *files.size[0], screen_size);
	if(ctx_full_image == NULL)
	{
		printf("Failed to create a new spng context\n");
		goto Exit;
	}

	// decode image and store in buffer
	out_size = decode_image(ctx_full_image, &out_full_image, out_size, fmt, &ihdr);
	if (!out_size)
	{
		printf("Failed to decode image\n");
		goto Exit;
	}

	// get current x and y scale
	scale.resize = get_resize_factor(vinfo, ihdr, &scale);

	// rescale main full image if needed
	if (scale.resize)
	{
		out_size = do_resize(&out_full_image, &ihdr, scale);
		offset.x /= scale.x;
		offset.y /= scale.y;
	}

	// draw full screen image on framebuffer
	memcpy(fbp, out_full_image, screen_size);

	// show other images in loop if available
	if (files.count > 1)
	{
		size_t animation_timer;
		animation_data_t animation_data;
		animation_data.vinfo = &vinfo;
		animation_data.finfo = &finfo;
		animation_data.fbp = fbp;
		animation_data.files = &files;
		animation_data.out_full_image = out_full_image;
		animation_data.out_full_image_animation = malloc(sizeof(uint8_t *));
		*animation_data.out_full_image_animation = NULL;
		animation_data.out_image = malloc(sizeof(uint8_t *));
		*animation_data.out_image = NULL;
		animation_data.scale = &scale;
		animation_data.offset = &offset;

		// init periodic timer
		initialize();

		files.current = 1;
		animation_timer = start_timer(1000 / framerate, show_next_image, TIMER_PERIODIC, &animation_data);

		while (!end_animation)
			usleep(5000);

		// finalize periodic timer
		finalize();

		if (*animation_data.out_full_image_animation)
			free(*animation_data.out_full_image_animation);
		free(animation_data.out_full_image_animation);

		if (*animation_data.out_image)
			free(*animation_data.out_image);
		free(animation_data.out_image);
	}

Exit:
	if (out_full_image)
		free(out_full_image);
	if (ctx_full_image)
		spng_ctx_free(ctx_full_image);
	if (fbp)
		munmap(fbp, vinfo.yres * finfo.line_length);
	if (fbfd)
		close(fbfd);
	for (i = 0; i < files.count; i++)
	{
		free(files.data[i]);
		free(files.size[i]);
	}
	if (files.data)
		free(files.data);
	if (files.size)
		free(files.size);

	return (EXIT_SUCCESS);
}
