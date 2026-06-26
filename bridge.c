#include "bridge.h"
#include "config.h"
#include "log.h"
#include "notify.h"
#include "network.h"

#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/select.h>

void run_bridge(void) {
    int iphone = -1, server = -1;
    
    for (int i = 0; i < 10 && running; i++) {
        if (iphone < 0) iphone = connect_nonblocking(iphone_port, 2000);
        if (server < 0) server = connect_nonblocking(vts_server_port, 2000);
        if (iphone >= 0 && server >= 0) break;
        usleep(200000);
    }

    if (iphone < 0 || server < 0) {
        LOGMSG_ERR("Bridge connection failed. iPhone:%d Server:%d", iphone, server);
        if (iphone >= 0) close(iphone);
        if (server >= 0) close(server);
        return;
    }

    LOGMSG_OK("Bridge active! Forwarding VTS data.");
    send_notification("VTS Bridge Active", "iPhone and VTS connected. Tracking is live!", "normal");

    int ep = epoll_create1(0);
    struct epoll_event ev1 = { .events = EPOLLIN, .data.fd = iphone };
    struct epoll_event ev2 = { .events = EPOLLIN, .data.fd = server };

    epoll_ctl(ep, EPOLL_CTL_ADD, iphone, &ev1);
    epoll_ctl(ep, EPOLL_CTL_ADD, server, &ev2);

    char buf[BUF_SIZE];
    struct epoll_event events[MAX_EVENTS];
    int active = 1;

    while (active && running) {
        int n = epoll_wait(ep, events, MAX_EVENTS, 3000);
        
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < n; i++) {
            int src = events[i].data.fd;
            int dst = (src == iphone) ? server : iphone;

            ssize_t r = recv(src, buf, BUF_SIZE, 0);

            if (r <= 0) {
                if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) continue;
                LOGMSG_WARN("Disconnected by %s", (src == iphone) ? "iPhone" : "VTS Server");
                active = 0;
                break;
            }

            ssize_t sent = 0;
            while (sent < r) {
                ssize_t w = send(dst, buf + sent, r - sent, 0);
                if (w < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) { usleep(100); continue; }
                    active = 0; break;
                }
                sent += w;
            }
        }
    }

    LOGMSG_WARN("Bridge disconnected.");
    close(iphone);
    close(server);
    close(ep);
}

// tl;dr keep iphone connected even if vts is not ready yet 

void drain_iphone_data(void) {
    int iphone = connect_nonblocking(iphone_port, 2000);
    if (iphone < 0) return;

    
    LOGMSG_INFO("Draining iPhone data while waiting for VTS...");

    char buf[BUF_SIZE];
    time_t last_check = time(NULL);
    time_t last_data = time(NULL);

    while (running) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(iphone, &readfds);
        
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int ret = select(iphone + 1, &readfds, NULL, NULL, &tv);
        
        if (ret > 0) {
            ssize_t n = recv(iphone, buf, BUF_SIZE, 0);
            if (n <= 0 && !(errno == EAGAIN || errno == EWOULDBLOCK)) {
                LOGMSG_WARN("iPhone disconnected during drain");
                break;
            }
            if (n > 0) {
                last_data = time(NULL);
                 send(iphone, buf, n, 0);
            }
        }
        
        time_t now = time(NULL);
        if (now - last_check >= VTS_CHECK_INTERVAL) {
            last_check = now;
            if (check_vts_available()) {
                LOGMSG_OK("VTS is now available!");
                send_notification("VTS Ready", "VTS server detected. Starting bridge...", "normal");
                close(iphone);
                vts_ready = 1;
                return;
            }
        }

        // uh oh
        if (now - last_data > KEEPALIVE_INTERVAL) {
            LOGMSG_WARN("No data from iPhone for %d seconds, reconnecting...", KEEPALIVE_INTERVAL);
            break;
        }
    }
    
    close(iphone);
}
