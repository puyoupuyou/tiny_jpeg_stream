#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <linux/fb.h>
#include <turbojpeg.h>
#include <popt.h>
#include "tjstream.h"

static int prepare_jpg_image(char* name, unsigned char* jpegBuf, int bufSize)
{
	FILE *jpegFile = NULL;
	long size;
	unsigned long jpegSize = 0;

	/* Read the JPEG file into memory. */
	if ((jpegFile = fopen(name, "rb")) == NULL) {
		fprintf(stderr, "opening input file %s", name);
		goto bailout;
	}
	if (fseek(jpegFile, 0, SEEK_END) < 0
		|| ((size = ftell(jpegFile)) < 0)
		|| fseek(jpegFile, 0, SEEK_SET) < 0) {
		fprintf(stderr, "determining input file size");
		goto bailout;
	}
	if (size == 0) {
		fprintf(stderr, "determining input file size, Input file contains no data");
		goto bailout;
	}
	jpegSize = (unsigned long)size;

	if(jpegBuf == NULL || bufSize < jpegSize) {
		fprintf(stderr, "jpegBuf %p or alloc size too samll %d(%ld)",
				jpegBuf, bufSize, jpegSize);
		goto bailout;
	}
	if (fread(jpegBuf, jpegSize, 1, jpegFile) < 1) {
		fprintf(stderr, "reading input file");
		goto bailout;
	}
	if (jpegFile) fclose(jpegFile);

bailout:
	return jpegSize;
}

void test_decode(char* name)
{
	unsigned long jpegSize;
	unsigned char *jpegBuf = NULL;
	unsigned char *imgBuf = NULL;
	unsigned char *fbmem = NULL;
	int fbfd = 0;
	int frames=203;
	int i, szjpegBuf, szimgBuf;
	int screensize;

	char name_array[255] = {0};

	szjpegBuf= 500*1024;
	szimgBuf= 1024 * 600 * 4;
	if ((jpegBuf = (unsigned char *)tjAlloc(szjpegBuf)) == NULL) {
		fprintf(stderr, "allocating JPEG buffer");
		goto bailout;
	}
	if ((imgBuf = (unsigned char *)tjAlloc(szimgBuf)) == NULL) {
		fprintf(stderr, "allocating uncompressed image buffer");
		goto bailout;
	}
	fbmem = create_fb_img(&fbfd, &screensize);
	for (i = 0; i < frames; i++) {
		memset(name_array,0, sizeof(name_array));
		sprintf(name_array,"%s/output-%03d.jpg", name, i+1);
		jpegSize = prepare_jpg_image(name_array, jpegBuf, szjpegBuf);
		decode_jpeg_by_cpu(jpegBuf, jpegSize, imgBuf, szimgBuf);
		flush_fb_image(imgBuf, fbmem, 1024 * 600 *3);

		msleep(8000000);
	}
bailout:
	destory_fb_image(fbmem, screensize, fbfd);
	if (imgBuf) tjFree(imgBuf);
	if (jpegBuf) tjFree(jpegBuf);
}

