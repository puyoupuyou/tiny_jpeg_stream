#ifndef __TJSTREAM_H__
#define __TJSTREAM_H__

#include <event.h>
#include <event2/util.h>
#include <arpa/inet.h>
#include <pthread.h>


#define __unused		__attribute__((unused))

#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
})

#define MAGIC			0x544a534d	/* TJSM */
#define kMessageHeaderMagic	MAGIC	
#define MAGICLEN		4
#define MSGLEN			12
#define IS_REQ(x) 		(x & 0x8000)
#define SET_REQ(x) 		(x | 0x8000)
#define CLEAR_REQ(x) 		(x & 0x7FFF)
#define SET_REPLY 		CLEAR_REQ
#define JMTYPE_REQ		0x8000
#define JMTYPE_REPLY		0

#define TJSREQ_RESOLUTION_REPORT	0x40
#define TJSREQ_STREAM_START		0x80
#define TJSREQ_STREAMING		0x81
#define TJSREQ_STREAMING_END		0x82

enum tjs_state_machine {
	/* connection */
	TJSSM_HOST_DISCONNECTED	= 0,
	TJSSM_DEVICE_DISCONNECTED,
	TJSSM_DEVICE_CONNECTED,
	/* network stage, change to p2p connect */
	TJSSM_P2P_CAP_REPORT	= 0x20,
	TJSSM_P2P_NEGOTIATE,
	TJSSM_P2P_SETUP,
	/* report stage */
	TJSSM_RESOLUTION_REPORT = 0x40,
	TJSSM_RESOLUTION_NEGOTIATE,
	TJSSM_RESOLUTION_CHOOSE,
	TJSSM_RESOLUTION_FINNAL,
	TJSSM_RESOLUTION_CHANGE_REQ,
	/* stream stage */
	TJSSM_STREAMING_REQ	= 0x80,
	TJSSM_STREAMING,
	TJSSM_STREAMING_END,
	/* done */
};

struct tiny_jpeg_stream_param {
	char ip[64];
	int port;
	int fps;
	unsigned int jfmt;
	int jflags;

	int loglevel;
	int enable_filelog;
	char log_dir[256];
	int test_mode;
	char test_dir[256];
	int server_mode;
};

struct tiny_jpeg_stream_info {
	/* fb info */
	char fbname[64];
	int fbfd;
	int screensize;
	unsigned char *fbmem;
	int fb_width;
	int fb_height;
	int fb_depth;
	int fb_yoff;

	/*jpeg info */
	int j_flags;
	int j_width;
	int j_height;
	int j_depth;
	int j_fmt;
	int szjpegBuf;
	unsigned char *jpegBuf;
	unsigned long jpegSize;
	unsigned char *imgBuf;
	int szimgBuf;

	/* link to param */
	void *param;
};

struct link_info {
	struct sockaddr_in server_addr;
	struct event_base *base;
	struct bufferevent *bev;
	struct event *ev_cmd;
};

struct tiny_jpeg_stream_mgr {
	//struct list_head list;
	struct tiny_jpeg_stream_info info;
	struct link_info linfo;
	int sm;
	pthread_t ntid;
	pthread_mutex_t lock;
};

/* socket state */
#define NET_EVENT_CONNECTED		1
#define NET_EVENT_RECV			2
#define NET_EVENT_SEND			4
#define NET_EVENT_EOF			8
#define NET_EVENT_ERROR			0x10
#define NET_EVENT_DISCONNECTED		0x20

/* debug level */
#define LNONE		0
#define LERROR		1
#define LWARN		2
#define LINFO		3
#define LVERBOSE	4
#define PREFIX "[CLIENT]"

struct msg_block {
	unsigned int magic;
	unsigned short jmtype;
	unsigned short rsvd;
	unsigned int len;
	void *payload;
};

void msleep(long nsec);

int tjs_net_start(struct tiny_jpeg_stream_mgr *mgr);
int tjs_net_stop(struct tiny_jpeg_stream_mgr *mgr);
int tjs_net_event_post(struct tiny_jpeg_stream_mgr *mgr, int event);
int tjs_check_result(int sm, void  *msg);
struct evbuffer* tjs_evmsg_pack(
		struct msg_block *blk,
		unsigned short type,
		int payload_len);
int tjs_evmsg_write(struct bufferevent *bev,
		struct evbuffer *evmsg, struct evbuffer *evbody,
		int cleanup);

int tjs_process_input(struct tiny_jpeg_stream_mgr *mgr);
int tjs_stop_connection(struct tiny_jpeg_stream_mgr *mgr);
int tjs_start_connection(struct tiny_jpeg_stream_mgr *mgr);
int tjs_start_stream(struct tiny_jpeg_stream_mgr *mgr);
int tjs_read_frame(struct tiny_jpeg_stream_mgr *mgr, size_t len);

extern void* create_fb_img(int *fbfd_out, int *screen_size);
extern int fb_stat(int fd, int *width, int *height, int *depth, int yoffset);
extern void *fb_mmap(int fd, unsigned int screensize);
extern int fb_munmap(void *start, size_t length);
extern int destory_fb_image(void* fbmem, int screensize, int fbfd);
static inline void flush_fb_image(void* data, void* fbmem, long size)
{
	memcpy((unsigned char *) fbmem , data, size);
}


extern long decode_jpeg_by_cpu(unsigned char *jpegBuf, unsigned long jpegSize,
			unsigned char *imgBuf, unsigned long imgSize);
extern void test_decode(char* name);

extern int tjstream_init_param(struct tiny_jpeg_stream_param *param);
extern int tjstream_get_param(int argc, char *argv[], struct tiny_jpeg_stream_param *param);
extern int utils_get_jfmt(char *string, unsigned int *fmt);
extern int utils_get_loglevel(char *string, int *loglevel);
extern int tjs_trigger_exit(int code);
extern int tjs_wait_for_exit();

#endif
