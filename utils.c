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
#include <popt.h>
#include <turbojpeg.h>
#include <pthread.h>
#include "tjstream.h"

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

int utils_get_jfmt(char *string, unsigned int *fmt)
{
	printf(PREFIX"%s need implment!\n", __func__);
	return 0;
}

int utils_get_loglevel(char *string, int *loglevel)
{
	printf("%s need implment!\n", __func__);
	return 0;
}
int tjs_trigger_exit(int code)
{
	printf(PREFIX"%s need implment!\n", __func__);
	return 0;
}
int tjs_wait_for_exit()
{
	printf(PREFIX"%s need implment!\n", __func__);
	return 0;
}
