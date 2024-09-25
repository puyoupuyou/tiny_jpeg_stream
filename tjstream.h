#ifndef __TJSTREAM_H__
#define __TJSTREAM_H__

#include <event.h>
#include <event2/util.h>
#include <arpa/inet.h>
#include <pthread.h>


#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
})

enum tjs_state_machine {
	TJSSM_HOST_DISCONNECTED = 0,
	TJSSM_DEVICE_CONNECTED,
	/* network stage, change to p2p connect */
	TJSSM_P2P_CAP_REPORT,
	TJSSM_P2P_NEGOTIATE,
	TJSSM_P2P_SETUP,
	/* report stage */
	TJSSM_RESOLUTION_REPORT,
	TJSSM_RESOLUTION_NEGOTIATE,
	TJSSM_RESOLUTION_CHOOSE,
	TJSSM_RESOLUTION_FINNAL,
	TJSSM_RESOLUTION_CHANGE_REQ,
	/* stream stage */
	TJSSM_STREAMING_REQ,
	TJSSM_STREAMING,
	TJSSM_STREAMING_END,
	/* done */
	TJSSM_DEVICE_DISCONNECTED,
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

#define LNONE		0
#define LERROR		1
#define LWARN		2
#define LINFO		3
#define LVERBOSE	4
#define PREFIX "[CLIENT]"


int tjs_net_start(struct tiny_jpeg_stream_mgr *mgr);
int tjs_net_stop(struct tiny_jpeg_stream_param *param);

int tjs_net_start_client(char* ip, int port, struct tiny_jpeg_stream_mgr *mgr);
void ev_msg_cb(int fd, short events, void*arg);
void bev_read_msg_cb(struct bufferevent *bev, void *arg);
void bev_cb(struct bufferevent* bev, short event, void *arg);

int tjs_process_reply(struct tiny_jpeg_stream_mgr *mgr);
int tjs_stop_connection(struct tiny_jpeg_stream_mgr *mgr);
int tjs_start_connection(struct tiny_jpeg_stream_mgr *mgr);
int tjs_start_stream(struct tiny_jpeg_stream_mgr *mgr);
#endif
