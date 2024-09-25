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
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <linux/fb.h>
#include <turbojpeg.h>
#include <popt.h>
#include <event.h>
#include <event2/util.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "tjstream.h"
//
struct timeval start, end;
long seconds, useconds;
double total_time;

int loglevel=0;
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

static inline void flush_fb_image(void* data, void* fbmem, long size)
{
	memcpy((unsigned char *) fbmem , data, size);
}

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

void msleep(long nsec)
{
    struct timespec ts;
    ts.tv_sec = 0; // 秒
    ts.tv_nsec = nsec;
    nanosleep(&ts, NULL);
}


int prepare_jpg_image(char* name, unsigned char* jpegBuf, int bufSize)
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

	char name_array[64] = {0};

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

/********************************************************
 *
 *
 ********************************************************/
int fb_ui_init(struct tiny_jpeg_stream_info *info)
{
	printf("%s need implment!\n", __func__);
	return 0;
}

int tjs_fb_init(struct tiny_jpeg_stream_info * info)
{
	unsigned char *fbmem = NULL;
	int width, height, depth, yoff=-1;
	int screensize;

	// Open the frame buffer device
	int fbfd = open(info->fbname, O_RDWR);
	if (fbfd < 0) {
		perror("Error opening frame buffer device");
		return -1;
	}

	fb_stat(fbfd, &width, &height, &depth, yoff);
	/* two time of resultion byte size? */
	screensize = width * height * depth / 8 * 2;

	//映射LCD内存
	fbmem = fb_mmap(fbfd, screensize);

	info->screensize= screensize;
	info->fbfd = fbfd;
	info->fbmem = fbmem;
	info->fb_width = width;
	info->fb_height = height;
	info->fb_depth  = depth;
	info->fb_yoff = yoff;
	return 0;
}
int tjs_fb_deinit(struct tiny_jpeg_stream_info * info)
{
	fb_munmap(info->fbmem, info->screensize);
	close(info->fbfd);
	return 0;
}

int tjs_decode_init(struct tiny_jpeg_stream_info * info)
{
	unsigned char *jpegBuf = NULL;
	unsigned char *imgBuf = NULL;

	/* decoded jpeg buffer size */
	info->szjpegBuf = 500*1024; 
	/**/
	info->j_width = 1024;
	info->j_height = 600;
	info->j_fmt = TJPF_BGR;
	/* assume max xrgb, one pixel need 4 bytes. */
	info->szimgBuf = info->j_width * info->j_height * 4;

	if ((jpegBuf = (unsigned char *)tjAlloc(info->szjpegBuf)) == NULL) {
		fprintf(stderr, "allocating JPEG buffer");
		goto bailout;
	}
	if ((imgBuf = (unsigned char *)tjAlloc(info->szimgBuf)) == NULL) {
		fprintf(stderr, "allocating uncompressed image buffer");
		goto bailout;
	}
	info->jpegBuf = jpegBuf;
	info->imgBuf  = imgBuf;
	return 0;
bailout:
	if (imgBuf) tjFree(imgBuf);
	if (jpegBuf) tjFree(jpegBuf);
	return 1;
}

int wait_image_ready(struct tiny_jpeg_stream_mgr *mgr, int timeout)
{
	printf(PREFIX"%s\n", __func__);
	pthread_mutex_lock(&mgr->lock);
	printf(PREFIX"%s get data\n", __func__);
	return 0;
}

int set_image_done(struct tiny_jpeg_stream_mgr *mgr)
{
	pthread_mutex_unlock(&mgr->lock);
	return 0;
}

int tjs_get_image(unsigned char* jpegBuf, int bufSize, unsigned long *jpegSize)
{
	printf("%s need implment!\n", __func__);
	return 0;
}
void* tjs_process(void *arg)
{
	struct tiny_jpeg_stream_mgr *mgr = (struct tiny_jpeg_stream_mgr *)arg;
	struct tiny_jpeg_stream_info *info = &mgr->info;
	struct tiny_jpeg_stream_param *param = info->param;
	int ret;


	/* one frame timeout=1000/fps */
	int timeout_usec = (1000 * 1000 ) / param->fps;
	
	long szimage = info->fb_width * info->fb_height * info->fb_depth / 8;

	while(1){
		ret = wait_image_ready(mgr, timeout_usec);
		if (ret)
			continue;
		ret = tjs_get_image(info->jpegBuf,info->szjpegBuf, &info->jpegSize);
		if (!ret) {
			decode_jpeg_by_cpu(info->jpegBuf,
					info->jpegSize,
					info->imgBuf,
					info->szimgBuf);
			flush_fb_image(info->imgBuf, info->fbmem, szimage);
		}
		set_image_done(mgr);
	}
	/**/
	return mgr;
}
int tjstream_init_param(struct tiny_jpeg_stream_param *param)
{
	/* net configuration */
	strcpy(param->ip,"0.0.0.0");
	param->port = 8923;
	param->fps = 20;

	/* JPEG configuration */
	param->jfmt = TJPF_BGR;
	param->jflags = TJFLAG_FASTDCT;

	/* tool configuration */
	param->loglevel = LINFO;
	param->enable_filelog = false;
	param->test_mode = false;
	param->server_mode = false;

	return 0;
}

int tjstream_init_info(struct tiny_jpeg_stream_info *info, void *param)
{
	strcpy(info->fbname, "/dev/fb0");
	info->param = param;
	return 0;
}

int tjstream_info_show(struct tiny_jpeg_stream_info *info)
{

	fprintf(stdout, "Info:\n");
	fprintf(stdout, "  fb: %s\n", info->fbname);
	fprintf(stdout, "  width : %d\n", info->fb_width);
	fprintf(stdout, "  height: %d\n", info->fb_height);
	fprintf(stdout, "  depth : %d\n", info->fb_depth);
	fprintf(stdout, "  yoffset: %d\n", info->fb_yoff);
	fprintf(stdout, "  fb size: 0x%x ( %d kb)\n", info->screensize, info->screensize >> 10);
	fprintf(stdout, "  JPEG\n");
	fprintf(stdout, "   buffer size  : %x (%d KB)\n", info->szjpegBuf, info->szjpegBuf >> 10);
	fprintf(stdout, "   default  width: %d\n", info->j_width);
	fprintf(stdout, "   default height: %d\n", info->j_height);
	fprintf(stdout, "   pixel format  : %d\n", info->j_fmt);
	fprintf(stdout, "\n");
	return 0;

}
int utils_get_jfmt(char *string, unsigned int *fmt)
{
	printf("%s need implment!\n", __func__);
	return 0;
}

int utils_get_loglevel(char *string, int *loglevel)
{
	printf("%s need implment!\n", __func__);
	return 0;
}
/*
 *  --fps-max
 *  --fps-min
 *  --frame-sync
 *  --client-mode(default)
 *  --server-mode
 *  --fb-res=widthxheight:bpp
 *  --fb-fmt=(RGB/BGR)
 *  --jpg-res=widthxheight:bpp
 *  --jfmt=RGB/RGB/RGBX...
 *  --test-mode
 *  --test-mode-dir=
 * */
int tjstream_get_param(int argc, char *argv[], struct tiny_jpeg_stream_param *param)
{
	int opt;
	int port, fps, jflags;
	char *ip, *log_dir, *loglevel, *jfmt;
	poptContext optCon;
	
	struct poptOption theOptions[] = {
	{"ip",		'a', POPT_ARG_STRING,	&ip,	'a', "ip address", "a.b.c.d"},
	{"port",	'p', POPT_ARG_INT,	&port,	'p', "port", "port"},
	{"fps",		'f', POPT_ARG_INT,	&fps,	'f', "fps", "fps"},
	{"jfmt",	't', POPT_ARG_STRING,	&jfmt,  't', "jpeg pixel format", "BRG/RGB/BGRX/RGBX"},
	{"jflags",	'F', POPT_ARG_INT,	&jflags,'F', "hex number override default jpeg jflags", "flags"},
	{"loglevel",	'l', POPT_ARG_STRING,	&loglevel,	'l', "loglevel", "NONE/ERROR/WARN/INFO/VERBOSE"},
	{"log-dir",	'd', POPT_ARG_STRING,	&log_dir,	'd', "dir name", "dir"},
	{"test-mode",	'T', POPT_ARG_NONE,	NULL,	'T', "test mode to check fps perf", ""},
	POPT_AUTOHELP { NULL, 0, 0, NULL ,0},
	};

	optCon = poptGetContext(NULL, argc, (const char **)argv, theOptions, 0);
	poptSetOtherOptionHelp(optCon, "[OPTION...]");

	while ((opt = poptGetNextOpt(optCon)) >= 0) {
		switch (opt) {
		case 'a':
			strncpy(param->ip, ip, sizeof(param->ip) - 1);
			break;
		case 'p':
			param->port = port;
			break;
		case 'f':
			param->fps = fps;
			break;
		case 't':
			utils_get_jfmt(jfmt, &param->jfmt);
			break;
		case 'F':
			param->jflags = jflags;
			break;
		case 'l':
			utils_get_loglevel(loglevel, &param->loglevel);
			break;
		case 'd':
			strncpy(param->log_dir, log_dir, sizeof(param->log_dir) - 1);
			break;
		default:
			break;
		}
	};
	if (opt < -1) {
		fprintf(stderr, "%s: %s\n",
			poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
			poptStrerror(opt));
		poptFreeContext(optCon);
		exit(EXIT_FAILURE);
	}
	poptFreeContext(optCon);
	printf("Params:\n");
	printf("  ip: %s:%d\n", param->ip,param->port);
	printf("  fps: %d\n", param->fps);
	printf("  server mode : %s\n", param->server_mode?"true":"false" );
	printf("  test mode   : %s\n", param->test_mode?  "true":"false" );
	printf("  loglevel    : %x\n", param->loglevel);
	printf("  filelog     : %s\n", param->enable_filelog? "true":"false" );
	printf("  Jpeg params:\n");
	printf("    pixel format: %x\n", param->jfmt);
	printf("    decode flags: %x\n", param->jflags);
	return 0;
}

int tjs_trigger_exit(int code)
{
	printf("%s need implment!\n", __func__);
	return 0;
}
int tjs_wait_for_exit()
{
	printf("%s need implment!\n", __func__);
	return 0;
}

int tjs_clean_up(struct tiny_jpeg_stream_info *info)
{
	printf("%s need implment!\n", __func__);
	tjs_fb_deinit(info);
	if (info->imgBuf) tjFree(info->imgBuf);
	if (info->jpegBuf) tjFree(info->jpegBuf);
	return 0;
}

int main(int argc, char *argv[])
{
	struct tiny_jpeg_stream_mgr *mgr;
	struct tiny_jpeg_stream_info *tjs_info;
	struct tiny_jpeg_stream_param *tjs_param;

	tjs_param = (struct tiny_jpeg_stream_param *)malloc(
				sizeof(struct tiny_jpeg_stream_param));
	memset((void*)tjs_param, 0, sizeof(struct tiny_jpeg_stream_param));

	mgr = (struct tiny_jpeg_stream_mgr *)malloc(
				sizeof(struct tiny_jpeg_stream_mgr));
	memset((void*)mgr, 0, sizeof(struct tiny_jpeg_stream_mgr));
	printf(PREFIX"%s mgr: %p!\n", __func__, (void*)mgr);

	mgr->info.param = tjs_param;
	tjs_info = &mgr->info;

	tjstream_init_param(tjs_param);
	tjstream_get_param(argc, argv, tjs_param);
	tjstream_init_info(tjs_info, (void*)tjs_param);

	if (tjs_param->test_mode) {
		test_decode(tjs_param->test_dir);
		tjs_trigger_exit(0);
	} else {
		/* create stream net */
		tjs_fb_init(tjs_info);
		tjs_decode_init(tjs_info);
		fb_ui_init(tjs_info);
		tjstream_info_show(tjs_info);
		tjs_net_start(mgr);
	}

	tjs_wait_for_exit();

	tjs_net_stop(tjs_param);
	tjs_clean_up(tjs_info);
	free(tjs_param);
	free(mgr);
	return 0;
}
/********************************************************
 *
 *
 ********************************************************/
int tjs_net_stop_client(char* ip, int port)
{
	printf(PREFIX"%s need implment!\n", __func__);
#if 0
	event_base_free(base);
	bufferevent_free(bev);
	event_free(ev);
#endif
	return 0;
}
int tjs_net_stop(struct tiny_jpeg_stream_param *param)
{
	if (param->server_mode) {
		printf(PREFIX"%s need implment!\n", __func__);
	} else {
		tjs_net_stop_client(param->ip, param->port);
	}
	return 0;
}
int tjs_net_start(struct tiny_jpeg_stream_mgr *mgr)
{
	struct tiny_jpeg_stream_param *param = mgr->info.param;
	if (param->server_mode) {
		printf(PREFIX"%s need implment!\n", __func__);
	} else {
		printf(PREFIX"%s client start!\n", __func__);
		tjs_net_start_client(param->ip, param->port, mgr);
	}
	return 0;
}
int tjs_net_start_client(char* ip, int port, struct tiny_jpeg_stream_mgr *mgr)
{
	struct link_info *linker = &mgr->linfo;
	struct sockaddr_in *server_addr = &linker->server_addr;
	struct event_base *base = event_base_new();
	struct bufferevent *bev;
	struct event *ev_cmd;
	int ret;
	
	bev = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
	if(!bev) {
		fprintf(stderr, "%s error!\n", __func__);
		return 1;
	}
	//监听终端输入事件
	ev_cmd = event_new(base, STDIN_FILENO, EV_READ | EV_PERSIST,
				ev_msg_cb, (void*) bev);
	event_add(ev_cmd, NULL);

	memset((void*)server_addr, 0, sizeof(struct sockaddr_in));
	server_addr->sin_family = AF_INET;
	server_addr->sin_port = htons(port);
	inet_aton(ip, &server_addr->sin_addr);
	ret = bufferevent_socket_connect(bev, (struct sockaddr*) server_addr,
				sizeof(struct sockaddr_in));
	if (ret) {
		goto __exit;
	}	
	// 设置读取水印为 1024 字节
	//bufferevent_setwatermark(bev, EV_READ, 1024, 0);
	//// 设置超时时间为 5 秒
	//struct timeval tv = {5, 0};
	//bufferevent_set_timeouts(bev, &tv, NULL);
	printf(PREFIX"%s setcb arg mgr: %p!\n", __func__, (void*)mgr);
	bufferevent_setcb(bev, bev_read_msg_cb, NULL, bev_cb, (void*) mgr);
	bufferevent_enable(bev, EV_READ | EV_PERSIST);

	linker->base = base;
	linker->bev = bev;
	linker->ev_cmd = ev_cmd;

	event_base_dispatch(base);
	fprintf(stdout,"finished \n");
	return 0;

__exit:
	event_base_free(base);
	return ret;
}

void ev_msg_cb(int fd, short events, void*arg)
{
	char msg[1024];
	int ret = read(fd, msg, sizeof(msg));
	if (ret < 0) {
		perror("read fail ");
		exit(1);
	}

	struct bufferevent *bev = (struct bufferevent*) arg;
	// Send input message to server
	if ( (ret >= 5) && !memcmp(msg,"jexit", 5)) {
		char msg[]="$JPEG-STREAMING-END$\0";
		bufferevent_write(bev, msg, strlen(msg));
		printf(PREFIX"%s: end stream\n", __func__);
	}
}

void bev_read_msg_cb(struct bufferevent *bev, void *arg)
{
	printf(PREFIX"%s : mgr? %p \n",__func__, arg);
	tjs_process_reply(arg);
}

void bev_cb(struct bufferevent* bev, short event, void *arg)
{
	struct tiny_jpeg_stream_mgr *mgr = (struct tiny_jpeg_stream_mgr *)arg;
	struct link_info *linker = &mgr->linfo;

	printf(PREFIX"%s arg mgr: %p!\n", __func__, (void*)mgr);

	if (event & BEV_EVENT_EOF) {
		printf(PREFIX"connection closed\n");
	} else if (event & BEV_EVENT_ERROR) {
		printf(PREFIX"some other error\n");
	} else if (event & BEV_EVENT_CONNECTED) {
		printf(PREFIX"the client has connected to server\n");
		if (mgr->sm == TJSSM_HOST_DISCONNECTED) {
			/* create jpegstream process */
			mgr->sm = TJSSM_DEVICE_CONNECTED;
			tjs_start_connection(mgr);
		} else {
			/* resume last connection? */
			printf(PREFIX"stream has established!\n");
		}
		return;
	}

	mgr->sm = TJSSM_HOST_DISCONNECTED;
	tjs_stop_connection(mgr);

	//自动关闭套接字和清空缓冲区
	bufferevent_free(bev);

	struct event *ev = linker->ev_cmd;
	event_free(ev);
}

int tjs_process_reply(struct tiny_jpeg_stream_mgr *mgr)
{
	unsigned char msg[128];
	struct bufferevent *bev = mgr->linfo.bev;
	size_t len = bufferevent_read(bev, msg, sizeof(msg));

	printf(PREFIX"[%d]%s: recv from server\n", mgr->sm, __func__);
	switch (mgr->sm) {
	case TJSSM_RESOLUTION_REPORT:
		printf(PREFIX"[%d]%s: resoltion report reply %s(%d)\n",
				mgr->sm, __func__, msg, len);
		tjs_start_stream(mgr);
		break;
	case TJSSM_STREAMING_REQ:
		printf(PREFIX"[%d]%s: stream req reply %s\n", mgr->sm, __func__, msg);
		mgr->sm = TJSSM_STREAMING;
		break;
	case TJSSM_STREAMING:
		/* read picture bound then send signal*/
		printf(PREFIX"[%d]%s: stream %s\n", mgr->sm, __func__, msg);
		break;
	case TJSSM_STREAMING_END:
		/* read picture bound then send signal*/
		break;

	}
	return 0;
}

int tjs_stop_stream(struct tiny_jpeg_stream_mgr *mgr)
{
	struct link_info *linker = &mgr->linfo;
	char msg[]="$JPEG-STREAMING-END$\0";

	mgr->sm = TJSSM_STREAMING_END;
	bufferevent_write(linker->bev, msg, strlen(msg));
	printf(PREFIX"%s: stop stream \n", __func__);
	pthread_mutex_destroy(&mgr->lock);
	return 0;
}

int tjs_start_stream(struct tiny_jpeg_stream_mgr *mgr)
{
	struct link_info *linker = &mgr->linfo;

	pthread_mutex_init(&mgr->lock, NULL);
	int retval = pthread_create(
			&mgr->ntid, NULL, tjs_process, mgr);

	if (retval != 0) {
		fprintf(stderr, "Error:unable to create thread\n");
		return retval;
	}
	printf(PREFIX"%s: Thread created successfully\n", __func__);

	mgr->sm = TJSSM_STREAMING_REQ;
	char msg[]="$JPEG-STREAMING-REQ$\0";
	bufferevent_write(linker->bev, msg, strlen(msg));
	printf(PREFIX"[%d]%s: stream req\n", mgr->sm, __func__);
	return 0;
}
static int tjs_connect_host(struct tiny_jpeg_stream_mgr *mgr)
{
	printf(PREFIX"%s need implment!\n", __func__);
	return 0;
}
/*
 *
 * int tjs_resolution(struct tiny_jpeg_stream_mgr *mgr)
 * @breif       send res TJSREQ_RESOLUTION_REPORT to host
 *              then transform sm to xxx
 *              execpt reply tag: $JPEG-STREAM-OKAY$
 * */
static int tjs_resolution(struct tiny_jpeg_stream_mgr *mgr)
{
	struct link_info *linker = &mgr->linfo;
	char msg[]="$JPEG-RESOLUTION-RQ$1024x600@20fps$$\0";

	mgr->sm = TJSSM_RESOLUTION_REPORT;
	bufferevent_write(linker->bev, msg, strlen(msg));
	printf(PREFIX"[1]%s: resoltion report\n", __func__);
	return 0;
}
int tjs_stop_connection(struct tiny_jpeg_stream_mgr *mgr)
{
	return tjs_stop_stream(mgr);
}
int tjs_start_connection(struct tiny_jpeg_stream_mgr *mgr)
{
	printf(PREFIX"%s\n", __func__);
	tjs_connect_host(mgr);
	tjs_resolution(mgr);
	return 0;
}
