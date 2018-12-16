#ifndef __GSP_UDP_H
#define __GSP_UDP_H

#include <sys/types.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GSP_UDP_LOOP_ONCE 0
#define GSP_UDP_LOOP_FOREVER 1

#define GSP_UDP_RECV_BUF_LEN_MIN 1024
#define GSP_UDP_RECV_BUF_LEN_MAX 65000 // 65507

struct gsp_udp;

typedef void (*gsp_udp_close_cb)(struct gsp_udp *udp);
typedef int (*gsp_udp_read_cb)(struct gsp_udp *udp, const void *buf, ssize_t len,
                              struct sockaddr *addr, socklen_t addr_len);

struct gsp_udp_operations {
	gsp_udp_close_cb close_cb;
	gsp_udp_read_cb read_cb;
};

struct gsp_udp_info {
	const char *ipaddr;
	int port;
	size_t recv_buf_len;
};

struct gsp_udp {
	int fd;

	char *recv_buf;
	size_t recv_buf_len;

	struct gsp_udp_operations ops;

	void *user_data;
};

int gsp_udp_init(struct gsp_udp *udp, struct gsp_udp_info *info);
int gsp_udp_close(struct gsp_udp *udp);
void gsp_udp_read_start(struct gsp_udp *udp, gsp_udp_read_cb read_cb);
void gsp_udp_read_stop(struct gsp_udp *udp);
ssize_t gsp_udp_write(struct gsp_udp *udp, const void *buf, size_t len,
                     const struct sockaddr *addr, socklen_t addr_len);
int gsp_udp_loop(struct gsp_udp *udp, int flags);

#ifdef __cplusplus
}
#endif
#endif
