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

static void ev_msg_cb(int fd, short events, void*arg);
static void bev_read_msg_cb(struct bufferevent *bev, void *arg);
static void bev_cb(struct bufferevent* bev, short event, void *arg);

void msleep(long nsec)
{
    struct timespec ts;
    ts.tv_sec = 0; // 秒
    ts.tv_nsec = nsec;
    nanosleep(&ts, NULL);
}

/********************************************************
 *
 * Display & UI layer
 *
 ********************************************************/
int fb_ui_init(struct tiny_jpeg_stream_info *info)
{
	tjs_warn("%s need implment!\n", __func__);
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

/********************************************************
 *
 * JPEG Codec
 *
 ********************************************************/
int tjs_decode_deinit(struct tiny_jpeg_stream_info * info)
{
	if (info->imgBuf) tjFree(info->imgBuf);
	if (info->jpegBuf) tjFree(info->jpegBuf);
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
	tjs_msg(PREFIX"%s\n", __func__);
	pthread_mutex_lock(&mgr->lock);
	tjs_msg(PREFIX"%s get data\n", __func__);
	return 0;
}

int set_image_done(struct tiny_jpeg_stream_mgr *mgr)
{
	pthread_mutex_unlock(&mgr->lock);
	return 0;
}

int tjs_get_image(unsigned char* jpegBuf, int bufSize, unsigned long *jpegSize)
{
	tjs_warn("%s need implment!\n", __func__);
	return 0;
}

void* tjs_process_thread(void *arg)
{
	struct tiny_jpeg_stream_mgr *mgr = (struct tiny_jpeg_stream_mgr *)arg;
	struct tiny_jpeg_stream_info *info = &mgr->info;
	struct tiny_jpeg_stream_param *param = info->param;
	int ret = 0 ;

	/* one frame timeout=1000/fps */
	int timeout_usec = (1000 * 1000 ) / param->fps;
	
	long szimage = info->fb_width * info->fb_height * info->fb_depth / 8;

	while(1){
		ret = wait_image_ready(mgr, timeout_usec);
		if (ret)
			continue;
		//dump_hex(info->jpegBuf+info->jpegSize - 1024 ,1024);
		ret = tjs_get_image(info->jpegBuf,info->szjpegBuf, &info->jpegSize);
		if (!ret) {
			decode_jpeg_by_cpu(info->jpegBuf,
					info->jpegSize,
					info->imgBuf,
					info->szimgBuf);
			flush_fb_image(info->imgBuf, info->fbmem, szimage);
		}
		//set_image_done(mgr);
	}
	/**/
	return mgr;
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

int tjs_clean_up(struct tiny_jpeg_stream_info *info)
{
	printf(PREFIX"%s need implment!\n", __func__);
	tjs_fb_deinit(info);
	tjs_decode_deinit(info);
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
	tjs_debug(PREFIX"%s mgr: %p!\n", __func__, (void*)mgr);

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

	/* check connection */
	tjs_clean_up(tjs_info);
	free(tjs_param);
	free(mgr);
	return 0;
}
/********************************************************
 *
 *
 ********************************************************/
int tjs_net_stop_client(struct tiny_jpeg_stream_mgr *mgr)
{
	struct link_info *linker = &mgr->linfo;
	struct bufferevent *bev = linker->bev;
#if 0
	event_base_free(base);
	bufferevent_free(bev);
	event_free(ev);
#endif
	mgr->sm = TJSSM_HOST_DISCONNECTED;
	tjs_stop_connection(mgr);

	//自动关闭套接字和清空缓冲区
	bufferevent_free(bev);

	struct event *ev = linker->ev_cmd;
	event_free(ev);
	return 0;
}

int tjs_net_stop(struct tiny_jpeg_stream_mgr *mgr)
{
	struct tiny_jpeg_stream_param *param = mgr->info.param;

	if (param->server_mode) {
		tjs_warn(PREFIX"%s need implment!\n", __func__);
	} else {
		tjs_net_stop_client(mgr);
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
	bufferevent_setcb(bev, bev_read_msg_cb, NULL, bev_cb, (void*) mgr);
	bufferevent_enable(bev, EV_READ | EV_PERSIST);

	linker->base = base;
	linker->bev = bev;
	linker->ev_cmd = ev_cmd;

	event_base_dispatch(base);
	tjs_warn("finished \n");
	return 0;

__exit:
	event_base_free(base);
	return ret;
}

int tjs_net_start(struct tiny_jpeg_stream_mgr *mgr)
{
	struct tiny_jpeg_stream_param *param = mgr->info.param;
	if (param->server_mode) {
		tjs_warn(PREFIX"%s need implment!\n", __func__);
	} else {
		tjs_warn(PREFIX"%s client start!\n", __func__);
		tjs_net_start_client(param->ip, param->port, mgr);
	}
	return 0;
}

static void ev_msg_cb(int fd, short events, void*arg)
{
	struct evbuffer *msgbuffer;
	char msg[1024];
	int ret = read(fd, msg, sizeof(msg));
	if (ret < 0) {
		perror("read fail ");
		exit(1);
	}

	struct bufferevent *bev = (struct bufferevent*) arg;
	// Send input message to server
	if ( (ret >= 5) && !memcmp(msg,"jexit", 5)) {
		msgbuffer = tjs_evmsg_pack((struct msg_block *)msg, TJSREQ_STREAMING_END | JMTYPE_REQ, 0);
		tjs_evmsg_write(bev, msgbuffer, NULL, true);
		tjs_warn(PREFIX"%s: end stream\n", __func__);
	}
}

static void bev_read_msg_cb(struct bufferevent *bev, void *arg)
{
	struct tiny_jpeg_stream_mgr *mgr = (struct tiny_jpeg_stream_mgr *)arg;
	tjs_net_event_post(mgr, NET_EVENT_RECV);
}

static void bev_cb(struct bufferevent* bev, short event, void *arg)
{
	struct tiny_jpeg_stream_mgr *mgr = (struct tiny_jpeg_stream_mgr *)arg;

	if (event & BEV_EVENT_EOF) {
		tjs_warn(PREFIX"connection closed\n");
		tjs_net_event_post(mgr, NET_EVENT_EOF);
	} else if (event & BEV_EVENT_ERROR) {
		printf(PREFIX"some other error\n");
		tjs_net_event_post(mgr, NET_EVENT_ERROR);
	} else if (event & BEV_EVENT_CONNECTED) {
		printf(PREFIX"the client has connected to server\n");
		tjs_net_event_post(mgr, NET_EVENT_CONNECTED);
	}
	return;
}

int tjs_stop_stream(struct tiny_jpeg_stream_mgr *mgr)
{
	struct evbuffer *msgbuffer;
	struct msg_block msg;

	mgr->sm = TJSSM_STREAMING_END;
	msgbuffer = tjs_evmsg_pack(&msg, TJSREQ_STREAMING_END | JMTYPE_REQ, 0);
	tjs_evmsg_write(mgr->linfo.bev, msgbuffer, NULL, true);
	pthread_mutex_destroy(&mgr->lock);
	printf(PREFIX"[%d]%s: stream end req\n", mgr->sm, __func__);
	return 0;
}

int tjs_start_stream(struct tiny_jpeg_stream_mgr *mgr)
{
	struct evbuffer *msgbuffer;
	struct msg_block msg;

	pthread_mutex_init(&mgr->lock, NULL);
	/* mutex init lock state */
	pthread_mutex_lock(&mgr->lock);
	int retval = pthread_create(
			&mgr->ntid, NULL, tjs_process_thread, mgr);

	if (retval != 0) {
		fprintf(stderr, "Error:unable to create thread\n");
		return retval;
	}
	printf(PREFIX"%s: Thread created successfully\n", __func__);

	mgr->sm = TJSSM_STREAMING_REQ;
	msgbuffer = tjs_evmsg_pack(&msg, TJSREQ_STREAM_START | JMTYPE_REQ, 0);
	tjs_evmsg_write(mgr->linfo.bev, msgbuffer, NULL, true);
	mgr->sm = TJSSM_STREAMING;

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
#define MSG3		"$1024x600@20fps$\n"
#define MSG3_SZ		16
	struct link_info *linker = &mgr->linfo;
	struct evbuffer *evmsg, *evbody;
	struct msg_block msg;

	mgr->sm = TJSSM_RESOLUTION_REPORT;

	evbody = evbuffer_new();
	evbuffer_add(evbody, MSG3, MSG3_SZ);
	evmsg = tjs_evmsg_pack(&msg, TJSREQ_RESOLUTION_REPORT | JMTYPE_REQ, MSG3_SZ);
	tjs_evmsg_write(linker->bev, evmsg, evbody, true);

	printf(PREFIX"[%02x]%s: TJSREQ_RESOLUTION_REPORT(%d)\n",
			mgr->sm, __func__, TJSSM_RESOLUTION_REPORT);
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


static size_t tjs_net_get_input_length(struct bufferevent *bev)
{
	struct evbuffer *evbuf = bufferevent_get_input(bev);
	return evbuffer_get_length(evbuf);
}

int tjs_msgblk_received(struct tiny_jpeg_stream_mgr *mgr)
{
	size_t len = tjs_net_get_input_length(mgr->linfo.bev);
	struct evbuffer *evbuf = bufferevent_get_input(mgr->linfo.bev);
	struct msg_block msgblk;

	if (len < MSGLEN) {
		printf(PREFIX"Not enough data %d\n", len);
		return 1;
	}

	/* copy out massage head */
	evbuffer_copyout(evbuf, &msgblk, sizeof(msgblk));
	if (ntohl(msgblk.magic) != kMessageHeaderMagic) {
		evbuffer_drain(evbuf, MSGLEN);
		printf(PREFIX"%s: magic %x err, drop %d data \n", __func__,
				ntohl(msgblk.magic), MSGLEN);
		return -1;
	}
	if (len < ntohl(msgblk.len)) {
		bufferevent_setwatermark(mgr->linfo.bev, EV_READ, ntohl(msgblk.len), 0);
		printf(PREFIX"%s: read more current %d/%d\n", __func__, len, ntohl(msgblk.len));
		return len;
	}
	bufferevent_setwatermark(mgr->linfo.bev, EV_READ, 0, 0);
	return 0;
}
int tjs_process_input(struct tiny_jpeg_stream_mgr *mgr)
{
	unsigned char msg[128];
	struct bufferevent *bev = mgr->linfo.bev;
	size_t len;
	int ret;

	memset(msg, 0, sizeof(msg));
	tjs_msg(PREFIX"[%d]%s: recv from server\n", mgr->sm, __func__);
	switch (mgr->sm) {
	case TJSSM_RESOLUTION_REPORT:
		len = tjs_net_get_input_length(bev);
		len = bufferevent_read(bev, msg, len);
		tjs_msg(PREFIX"[%d]%s: resoltion report reply %s(%d)\n",
				mgr->sm, __func__, msg, len);
		tjs_start_stream(mgr);
		break;
	case TJSSM_STREAMING_REQ:
		len = tjs_net_get_input_length(bev);
		tjs_msg(PREFIX"[%d]%s: stream req reply %d\n", mgr->sm, __func__, len);
		len = bufferevent_read(bev, msg, len);
		ret = tjs_check_result(TJSREQ_STREAM_START, msg);
		if(!ret)
			mgr->sm = TJSSM_STREAMING;
		break;
	case TJSSM_STREAMING:
		len = tjs_net_get_input_length(bev);
		tjs_msg(PREFIX"[%d]%s: streaming %d\n", mgr->sm, __func__, len);
		/* read picture bound then send signal*/
		ret = tjs_read_frame(mgr, len);
		if (!ret) {
			set_image_done(mgr);
		} else {
			tjs_msg(PREFIX"[%d]%s: stream in\n", mgr->sm, __func__);
		}
		break;
	case TJSSM_STREAMING_END:
		len = tjs_net_get_input_length(bev);
		len = bufferevent_read(bev, msg, len);
		tjs_msg(PREFIX"[%d]%s: end stream reply %s(%d)\n",
				mgr->sm, __func__, msg, len);
		/* read picture bound then send signal*/
		break;
	default:
		len = bufferevent_read(bev, msg, sizeof(msg) - 1);
		tjs_msg(PREFIX"[%d]%s: unknown reply %s(%d)\n",
				mgr->sm, __func__, msg, len);
	}
	return 0;
}

int tjs_net_event_post(struct tiny_jpeg_stream_mgr *mgr, int event)
{
	switch (event){
	case NET_EVENT_CONNECTED:
		if (mgr->sm == TJSSM_HOST_DISCONNECTED) {
			/* create jpegstream process */
			mgr->sm = TJSSM_DEVICE_CONNECTED;
			tjs_start_connection(mgr);
		} else {
			/* resume last connection? */
			printf(PREFIX"stream has established!\n");
		}
		break;
	case NET_EVENT_RECV:
		if (!tjs_msgblk_received(mgr))
			tjs_process_input(mgr);
		break;
	case NET_EVENT_EOF:
	case NET_EVENT_ERROR:
		tjs_net_stop(mgr);
		break;
	}
	return 0;
}
int tjs_net_process_input()
{
	return 0;
}

int tjs_check_result(int sm, void  *msg)
{
	return 0;
}

__unused static void*
search_tag(unsigned char *str, int str_len, unsigned char *tag, int tag_len, char *ending)
{
	if ( str == NULL || tag == NULL)
		return NULL;
	if ( !str_len || !tag_len || ( str_len < tag_len ))
		return NULL;

	unsigned char *pt = tag;
	unsigned char *ps = str;

	str_len = str_len - tag_len;
	while (str_len--) {
		if ((pt - tag) == tag_len)
			return (ps - tag_len);
		if (*pt != *ps)
			pt = tag;
		else
			pt++;
		ps++;
	}
	return NULL;
}

void dump_hex(unsigned char *data, int len)
{
	unsigned char *p=data;
	int i= 0;
	for (i = 0 ; i < len ; i++ , p++) {
		if (!(i%16))
			printf("%04x:", (unsigned int)(p - data));
		printf(" %02x",*p);
		if( !((i+1)%16))
			printf("\n");
	}
	printf("\n");

}
int tjs_read_frame(struct tiny_jpeg_stream_mgr *mgr, size_t len)
{
	struct bufferevent *bev = mgr->linfo.bev;
	struct tiny_jpeg_stream_info *info = &mgr->info;
	struct msg_block *msg;
	int ret = 0;
	unsigned long jsize = 0;
	unsigned char head[MSGLEN];
	unsigned short type;
	
	struct evbuffer *evbuf = bufferevent_get_input(bev);
	/* copy out massage head */
	evbuffer_copyout(evbuf, head, sizeof(head));
	msg = (struct msg_block *)head;
	if ( ntohl(msg->magic) != kMessageHeaderMagic) {
		evbuffer_drain(evbuf, MSGLEN);
		printf(PREFIX"%s: magic %x err, drop %d data \n", __func__,
				ntohl(msg->magic), MSGLEN);
		return -1;
	}
	type = ntohs(msg->jmtype);
	if(type != SET_REPLY(TJSREQ_STREAMING)) {
		printf(PREFIX"%s: type %x err, drop %d data \n", __func__,
				ntohl(msg->jmtype), MSGLEN);
		evbuffer_drain(evbuf, MSGLEN);
		return -2;
	}

	if (len >= ntohl(msg->len)) {
		/* copy msg head */
		evbuffer_remove(evbuf, head, sizeof(head));
		/* copy to body */
		jsize = ntohl(msg->len) - MSGLEN;
		len = evbuffer_remove(evbuf, (unsigned char*)info->jpegBuf, jsize);
		bufferevent_setwatermark(bev, EV_READ, 0, 0);
		info->jpegSize = jsize;
	} else if (len < ntohl(msg->len)) {
		bufferevent_setwatermark(bev, EV_READ, ntohl(msg->len), 0);
		printf(PREFIX"%s: read more current %d \n", __func__, len );
		return len;
	}
	tjs_msg(PREFIX"%s read jpeg %d/%ld done !\n", __func__, len, jsize);
	return ret;
}

struct evbuffer* tjs_evmsg_pack(
		struct msg_block *blk,
		unsigned short type,
		int payload_len)
{
	int len = MSGLEN + payload_len;

	blk->magic = htonl(MAGIC);
	blk->jmtype = htons(type);
	blk->len = htonl(len);

	struct evbuffer *output_buffer = evbuffer_new();
	if (output_buffer == NULL) {
		return NULL;
	}
	evbuffer_add(output_buffer, blk, MSGLEN);
	return output_buffer;
}

int tjs_evmsg_write(struct bufferevent *bev,
		struct evbuffer *evmsg, struct evbuffer *evbody,
		int cleanup)

{
	struct evbuffer *output_buffer = bufferevent_get_output(bev);

	evbuffer_add_buffer(output_buffer, evmsg);
	if (cleanup)
		evbuffer_free(evmsg);
	if (evbody){
		evbuffer_add_buffer(output_buffer, evbody);
		evbuffer_free(evbody);
	}

	bufferevent_write_buffer(bev, output_buffer);

	return 0;
}


