#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef _WIN32
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>

    typedef SOCKET SOCKET_T;
    #define INVALID_SOCKFD INVALID_SOCKET
    #define CLOSESOCKET(s) closesocket(s)

    static inline int SET_NONBLOCK(SOCKET_T s) {
        u_long mode = 1;
        return ioctlsocket(s, FIONBIO, &mode);
    }

    #define SOCKET_ERRNO       WSAGetLastError()
    #define WOULDBLOCK_ERR     WSAEWOULDBLOCK
    #define INPROGRESS_ERR     WSAEWOULDBLOCK
    #define CONNREFUSED_ERR    WSAECONNREFUSED

    #ifndef MSG_NOSIGNAL
        #define MSG_NOSIGNAL 0
    #endif

    static inline int platform_init(void) {
        WSADATA wsa;
        return WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    static inline void platform_cleanup(void) {
        WSACleanup();
    }
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    #include <sys/select.h>

    typedef int SOCKET_T;
    #define INVALID_SOCKFD (-1)
    #define CLOSESOCKET(s) close(s)

    static inline int SET_NONBLOCK(SOCKET_T s) {
        int flags = fcntl(s, F_GETFL, 0);
        if (flags < 0) return -1;
        return fcntl(s, F_SETFL, flags | O_NONBLOCK);
    }

    #define SOCKET_ERRNO       errno
    #define WOULDBLOCK_ERR     EWOULDBLOCK
    #define INPROGRESS_ERR     EINPROGRESS
    #define CONNREFUSED_ERR    ECONNREFUSED

    static inline int platform_init(void) { return 0; }
    static inline void platform_cleanup(void) {}
#endif

#endif /* PLATFORM_H */
