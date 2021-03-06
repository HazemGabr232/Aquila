#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <fb.h>

#define _NJ_INCLUDE_HEADER_ONLY
#include "nanojpeg.c"

/* Framebuffer API */
static struct fb_fix_screeninfo fix_screeninfo;
static struct fb_var_screeninfo var_screeninfo;
static int fb = -1;
static unsigned xres, yres, line_length;

#define COLORMERGE(f, b, c)	((b) + (((f) - (b)) * (c) >> 8u))
#define _R(c)   (((c) >> 3*8) & 0xFF)
#define _G(c)   (((c) >> 2*8) & 0xFF)
#define _B(c)   (((c) >> 1*8) & 0xFF)
#define _A(c)   (((c) >> 0*8) & 0xFF)

void fb_put_pixel(struct fbterm_ctx *ctx, int x, int y, uint32_t color)
{
    unsigned char a = _A(color);
    unsigned char *p = &ctx->backbuf[y * line_length + 3*x];

    p[0] = COLORMERGE(_R(color), p[0], a);  /* Red */
    p[1] = COLORMERGE(_G(color), p[1], a);  /* Green */
    p[2] = COLORMERGE(_B(color), p[2], a);  /* Blue */
}

void fb_clear(struct fbterm_ctx *ctx)
{
    memset(ctx->backbuf, 0, yres*line_length);

    if (ctx->wallpaper)
        memcpy(ctx->backbuf, ctx->wallpaper, yres*line_length);
}

void fb_render(struct fbterm_ctx *ctx)
{
    lseek(fb, 0, SEEK_SET);
    write(fb, ctx->backbuf, line_length * yres);
}

void fb_term_init(struct fbterm_ctx *ctx)
{
    ctx->rows = yres/ctx->font->rows;
    ctx->cols = xres/ctx->font->cols;

    //char *wallpaper;
    //char *textbuf;
    ctx->backbuf = calloc(1, yres * line_length);
}

int fb_cook_wallpaper(struct fbterm_ctx *ctx, char *path)
{
    int img = open(path, O_RDONLY);
    size_t size = lseek(img, 0, SEEK_END);
    lseek(img, 0, SEEK_SET);

    char *buf = calloc(1, size);
    read(img, buf, size);
    close(img);

    njInit();
    int err = 0;

    if (err = njDecode(buf, size)) {
        free(buf);
        fprintf(stderr, "Error decoding input file: %d\n", err);
        return -1;
    }

    free(buf);
    size_t height = njGetHeight();
    size_t width  = njGetWidth();
    size_t cook_height, cook_width;
    char *img_buf = njGetImage();
    size_t ncomp = njGetImageSize() / (height * width);
    size_t img_line_length = width * ncomp;
    size_t fb_ncomp = 3;
    size_t xpan = 0, ypan = 0, xoffset = 0, yoffset = 0;

    /* Pan or crop image to match screen size */
    if (width < xres) {
        xpan = (xres-width)/2;
        cook_width = width;
    } else if (width > xres) {
        xoffset = (width-xres)/2;
        cook_width = xres;
    }

    if (height < yres) {
        ypan = (yres-height)/2;
        cook_height = height;
    } else if (height > yres) {
        yoffset = (height-yres)/2;
        cook_height = yres;
    }

    ctx->wallpaper = calloc(1, yres*line_length);

#define WP_POS(i, j) (((i) + ypan) * line_length + ((j) + xpan) * fb_ncomp)
#define IMG_POS(i, j) (((i) + yoffset) * img_line_length + ((j) + xoffset) * ncomp)

    for (size_t i = 0; i < cook_height; ++i) {
        for (size_t j = 0; j < cook_width; ++j) {
            ctx->wallpaper[WP_POS(i, j) + 2] = img_buf[IMG_POS(i, j) + 0];
            ctx->wallpaper[WP_POS(i, j) + 1] = img_buf[IMG_POS(i, j) + 1];
            ctx->wallpaper[WP_POS(i, j) + 0] = img_buf[IMG_POS(i, j) + 2];
        }
    }

    njDone();

    memcpy(ctx->backbuf, ctx->wallpaper, yres*line_length);
    return 0;
}

int fb_init(char *path)
{
    if ((fb = open(path, O_RDWR)) < 0) {
        fprintf(stderr, "Error opening framebuffer %s", path);
        perror("");
        return errno;
    }

    if (ioctl(fb, FBIOGET_FSCREENINFO, &fix_screeninfo) < 0) {
        perror("ioctl error");
        return errno;
    }

    line_length = fix_screeninfo.line_length;

    if (ioctl(fb, FBIOGET_VSCREENINFO, &var_screeninfo) < 0) {
        perror("ioctl error");
        return errno;
    }

    xres = var_screeninfo.xres;
    yres = var_screeninfo.yres;

    return 0;
}
