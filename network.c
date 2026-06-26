#include "network.h"
#include "config.h"

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

int connect_nonblocking(int port, int timeout_ms) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;

    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = inet_addr("127.0.0.1")
    };

    int ret = connect(s, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        close(s);
        return -1;
    }

    if (ret < 0) {
        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(s, &writefds);
        
        struct timeval tv = {
            .tv_sec = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000
        };
        
        ret = select(s + 1, NULL, &writefds, NULL, &tv);
        if (ret <= 0) {
            close(s);
            return -1;
        }
        
        int error = 0;
        socklen_t len = sizeof(error);
        getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &len);
        if (error != 0) {
            close(s);
            return -1;
        }
    }

    int flag = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));
    
    return s;
}

int check_vts_available(void) {
    int s = connect_nonblocking(vts_server_port, 1000);
    if (s >= 0) {
        close(s);
        return 1;
    }
    return 0;
}

int probe_iphone_for_vts_data(int timeout_sec) {
    int s = connect_nonblocking(iphone_port, 2000);
    if (s < 0) return 0;

    fd_set readfds;
    struct timeval tv = { .tv_sec = timeout_sec, .tv_usec = 0 };
    
    FD_ZERO(&readfds);
    FD_SET(s, &readfds);
    
    int ret = select(s + 1, &readfds, NULL, NULL, &tv);
    if (ret > 0) {
        char buf[256];
        ssize_t n = recv(s, buf, sizeof(buf), MSG_PEEK);
        close(s);
        return (n > 0);
    }
    
    close(s);
    return 0;
}
