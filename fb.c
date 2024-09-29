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

extern int loglevel;

int fb_stat(int fd, int *width, int *height, int *depth, int yoffset)
{
	//FBIOGET_FSCREENINFO和FBIOGET_VSCREENINFO。前者返回与Framebuffer有关的固定的信息，比如图形硬件上实际的帧缓存空间的大小、能否硬件加速等信息。而后者返回的是与Framebuffer有关的可变信息。
	struct fb_fix_screeninfo fb_finfo;
	struct fb_var_screeninfo fb_vinfo;
	if (ioctl(fd, FBIOGET_FSCREENINFO, &fb_finfo)) {//返回framebuffer物理信息
		perror(__func__);
		return (-1);
	}
	// Get the screen dimensions
	if (ioctl(fd, FBIOGET_VSCREENINFO, &fb_vinfo)) {
		fprintf(stderr, "Error reading variable information");
		perror(__func__);
		return (-1);
	}

	if(fb_vinfo.bits_per_pixel != 24
	  && fb_vinfo.bits_per_pixel != 16){
		fprintf(stderr, "bpp %d not support!",fb_vinfo.bits_per_pixel);
		return (-1);
	}

	*width = fb_vinfo.xres;//宽度
	*height = fb_vinfo.yres;//高度
	*depth = fb_vinfo.bits_per_pixel;//var.bits_per_pixel为16每像素二进制位数

	if (loglevel) fprintf(stdout, "%s: vyres %d vxres %d\n",__func__, fb_vinfo.yres_virtual, fb_vinfo.xres_virtual);
	if (loglevel) fprintf(stdout, "%s: off(r/g/b) %d/%d/%d\n",__func__, fb_vinfo.red.offset, fb_vinfo.green.offset, fb_vinfo.blue.offset);
	if(yoffset<0) {
		return (0);
	}
	fb_vinfo.yoffset = yoffset;
	//返回framebuffer可变信息
	if (ioctl(fd, FBIOPAN_DISPLAY, &fb_vinfo)) {
		perror(__func__);
		return (-1);
	}
	if (ioctl(fd, FBIOGET_VSCREENINFO, &fb_vinfo)) {
		perror(__func__);
		return (-1);
	}

	if (loglevel) fprintf(stdout, "%s: yoff %d\n",__func__, fb_vinfo.yoffset);
	return (0);
}

//映射内存
void *fb_mmap(int fd, unsigned int screensize)
{
	caddr_t fbmem;
	//映射内存缓冲区
	fbmem = mmap(0, screensize, PROT_READ | PROT_WRITE,MAP_SHARED, fd, 0);
	if (fbmem == MAP_FAILED) {
		perror(__func__);
		return NULL;
	}
	return (fbmem);//返回缓冲区指针
}
//释放framebuffer内存
int fb_munmap(void *start, size_t length)
{
	if(start) return (munmap(start, length));
	return 0;
}

unsigned short RGB888toRGB565(unsigned char red, unsigned char green, unsigned char blue)
{
	unsigned short B = (blue >> 3) & 0x001F;//取高5位
	unsigned short G = ((green >> 2) << 5) & 0x07E0;//取高6位，并移动位置
	unsigned short R = ((red >> 3) << 11) & 0xF800;//取高5位并移动位置
	return (unsigned short) (R | G | B);//合并为16的像素数据
}

void* create_fb_img(int *fbfd_out, int *screen_size)
{
	unsigned char *fbmem = NULL;
	int width, height, depth, yoff=-1;
	int screensize;

	// Open the frame buffer device
	int fbfd = open("/dev/fb0", O_RDWR);
	if (fbfd < 0) {
		perror("Error opening frame buffer device");
		return NULL;
	}

	fb_stat(fbfd, &width, &height, &depth, yoff);
	fprintf(stdout, "fb width: %d height: %d depth: %d\n", width, height, depth);
	screensize = width * height * depth / 8 * 2;
	//映射LCD内存
	fbmem = fb_mmap(fbfd, screensize);
	*screen_size= screensize;

	*fbfd_out = fbfd;
	return fbmem;
}
int destory_fb_image(void* fbmem, int screensize, int fbfd)
{
	fb_munmap(fbmem, screensize);
	close(fbfd);
	return 0;
}
