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
#include <pthread.h>

#define _throw(action, message) { \
	fprintf(stderr, "ERROR in line %d while %s:\n%s\n", __LINE__, action, message); \
	goto bailout; \
}
#define _throwtj(action)  _throw(action, tjGetErrorStr2(tjInstance))

const char *subsampName[TJ_NUMSAMP] = {
  "4:4:4", "4:2:2", "4:2:0", "Grayscale", "4:4:0", "4:1:1"
};

const char *colorspaceName[TJ_NUMCS] = {
  "RGB", "YCbCr", "GRAY", "CMYK", "YCCK"
};

extern int loglevel;
long decode_jpeg_by_cpu(unsigned char *jpegBuf, unsigned long jpegSize,
			unsigned char *imgBuf, unsigned long imgSize)
{
	tjhandle tjInstance = NULL;
	int width, height;
	int inSubsamp, inColorspace;
	int pixelFormat = TJPF_BGR;
	int flags = 0;
	long size = 0;

	// 创建一个TurboJPEG解码器实例
	if ((tjInstance = tjInitDecompress()) == NULL)
		_throwtj("initializing compressor");
	if (tjDecompressHeader3(tjInstance, jpegBuf, jpegSize, &width, &height,
                            &inSubsamp, &inColorspace) < 0)
		_throwtj("reading JPEG header");

	if (loglevel) fprintf(stdout, "%s Image:  %d x %d pixels, %s subsampling, %s colorspace\n",
		   "Input", width, height,
		   subsampName[inSubsamp], colorspaceName[inColorspace]);

	size = width * height * tjPixelSize[pixelFormat];
	if( size > imgSize)
		_throwtj("allocating uncompressed image buffer");
	//printf("Using fast upsampling code\n");
	//flags |= TJFLAG_FASTUPSAMPLE;
	//printf("Using fastest DCT/IDCT algorithm\n");
	flags |= TJFLAG_FASTDCT;
	//printf("Using most accurate DCT/IDCT algorithm\n");
	//flags |= TJFLAG_ACCURATEDCT;
	pixelFormat = TJPF_BGR;
	if (tjDecompress2(tjInstance, jpegBuf, jpegSize,
				imgBuf,
				width, 0, height,
				pixelFormat, flags) < 0)
		_throwtj("decompressing JPEG image");

bailout:
	if (tjInstance) tjDestroy(tjInstance);
	return size;
}

