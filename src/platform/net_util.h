#ifndef NET_UTIL_H
#define NET_UTIL_H

#include "platform.h"

/* Read exactly n bytes from socket. Returns 0 on success, -1 on error. */
static inline int recv_all(SOCKET_T fd, void *buf, int n) {
    int done = 0;
    while (done < n) {
        int r = recv(fd, (char *)buf + done, n - done, 0);
        if (r <= 0) {
            if (r < 0 && SOCKET_ERRNO == WOULDBLOCK_ERR) continue;
            return -1;
        }
        done += r;
    }
    return 0;
}

#endif /* NET_UTIL_H */
