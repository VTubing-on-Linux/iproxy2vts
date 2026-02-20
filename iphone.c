
/*

 * Parts of this code are from iproxy, a libimobiledevice tool
 *
 * Following header is from iproxy.c:
 * =============================================================================
 * * iproxy.c -- proxy that enables tcp service access to iOS devices
 *
 * Copyright (C) 2009-2020 Nikias Bassen <nikias@gmx.li>
 * Copyright (C) 2014      Martin Szulecki <m.szulecki@libimobiledevice.org>
 * Copyright (C) 2009      Paul Sladen <libiphone@paul.sladen.org>
 *
 * Based upon iTunnel source code, Copyright (c) 2008 Jing Su.
 * http://www.cs.toronto.edu/~jingsu/itunnel/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * =============================================================================
 * 
 * libimobiledevice is licenced under GNU GPL licence, same as this project. You can find
 * the full license text in the LICENSE file included with this project.
 */

#include "iphone.h"
#include "config.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <usbmuxd.h>

static volatile int iproxy_running = 0;
static int listen_fd = -1;
static pthread_t listener_thread;

int iphone_connected_and_paired(void) {
    char **udids = NULL;
    int count = 0;

    if (idevice_get_device_list(&udids, &count) != IDEVICE_E_SUCCESS || count == 0)
        return 0;

    idevice_t dev = NULL;
    lockdownd_client_t lockdown = NULL;

    if (idevice_new(&dev, udids[0]) != IDEVICE_E_SUCCESS) {
        idevice_device_list_free(udids);
        return 0;
    }

    if (lockdownd_client_new_with_handshake(dev, &lockdown, "iproxy2vts") != LOCKDOWN_E_SUCCESS) {
        idevice_free(dev);
        idevice_device_list_free(udids);
        return 0;
    }

    lockdownd_client_free(lockdown);
    idevice_free(dev);
    idevice_device_list_free(udids);
    return 1;
}


struct proxy_client {
    int client_fd;
};

static void *proxy_handler(void *arg) {
    struct proxy_client *pc = (struct proxy_client *)arg;
    int cfd = pc->client_fd;
    free(pc);

    /* find first USB device via usbmuxd */
    usbmuxd_device_info_t *dev_list = NULL;
    int count = usbmuxd_get_device_list(&dev_list);
    if (count < 0 || !dev_list || dev_list[0].handle == 0) {
        LOGMSG_ERR("iproxy: no connected device found");
        free(dev_list);
        close(cfd);
        return NULL;
    }

    usbmuxd_device_info_t *dev = NULL;
    for (int i = 0; i < count; i++) {
        if (dev_list[i].conn_type == CONNECTION_TYPE_USB) {
            dev = &dev_list[i];
            break;
        }
    }

    if (!dev) {
        LOGMSG_ERR("iproxy: no USB device found");
        free(dev_list);
        close(cfd);
        return NULL;
    }

    int dfd = usbmuxd_connect(dev->handle, IPHONE_PORT);
    free(dev_list);

    if (dfd < 0) {
        LOGMSG_ERR("iproxy: connect to device port %d failed: %s",
                    IPHONE_PORT, strerror(-dfd));
        close(cfd);
        return NULL;
    }

    char buf[32768];
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(cfd, &fds);
    FD_SET(dfd, &fds);
    int maxfd = (cfd > dfd) ? cfd : dfd;

    while (1) {
        fd_set rfds = fds;
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);

        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;

        if (FD_ISSET(cfd, &rfds)) {
            ssize_t r = recv(cfd, buf, sizeof(buf), 0);
            if (r <= 0) break;
            ssize_t sent = 0;
            while (sent < r) {
                ssize_t w = send(dfd, buf + sent, r - sent, 0);
                if (w <= 0) goto done;
                sent += w;
            }
        }

        if (FD_ISSET(dfd, &rfds)) {
            ssize_t r = recv(dfd, buf, sizeof(buf), 0);
            if (r <= 0) break;
            ssize_t sent = 0;
            while (sent < r) {
                ssize_t w = send(cfd, buf + sent, r - sent, 0);
                if (w <= 0) goto done;
                sent += w;
            }
        }
    }

done:
    close(cfd);
    close(dfd);
    return NULL;
}


static void *iproxy_listener(void *arg) {
    (void)arg;

    while (iproxy_running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd, &rfds);

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int ret = select(listen_fd + 1, &rfds, NULL, NULL, &tv);

        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;

        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(listen_fd, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) continue;

        LOGMSG_INFO("iproxy: new connection (fd %d)", cfd);

        struct proxy_client *pc = malloc(sizeof(*pc));
        if (!pc) { close(cfd); continue; }
        pc->client_fd = cfd;

        pthread_t handler;
        if (pthread_create(&handler, NULL, proxy_handler, pc) == 0) {
            pthread_detach(handler);
        } else {
            LOGMSG_ERR("iproxy: failed to create handler thread");
            close(cfd);
            free(pc);
        }
    }

    return NULL;
}


void start_iproxy(void) {
    if (iproxy_running) return;

    LOGMSG_INFO("Starting built-in iproxy on port %d...", IPHONE_PORT);

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        LOGMSG_ERR("iproxy: socket(): %s", strerror(errno));
        return;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(IPHONE_PORT),
        .sin_addr.s_addr = inet_addr("127.0.0.1")
    };

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOGMSG_ERR("iproxy: bind port %d: %s", IPHONE_PORT, strerror(errno));
        close(listen_fd);
        listen_fd = -1;
        return;
    }

    if (listen(listen_fd, 5) < 0) {
        LOGMSG_ERR("iproxy: listen(): %s", strerror(errno));
        close(listen_fd);
        listen_fd = -1;
        return;
    }

    int flags = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);

    iproxy_running = 1;

    if (pthread_create(&listener_thread, NULL, iproxy_listener, NULL) != 0) {
        LOGMSG_ERR("iproxy: failed to create listener thread: %s", strerror(errno));
        close(listen_fd);
        listen_fd = -1;
        iproxy_running = 0;
        return;
    }

    LOGMSG_OK("Built-in iproxy listening on 127.0.0.1:%d", IPHONE_PORT);
}

void stop_iproxy(void) {
    if (!iproxy_running) return;

    iproxy_running = 0;

    if (listen_fd >= 0) {
        close(listen_fd);
        listen_fd = -1;
    }

    pthread_join(listener_thread, NULL);
    LOGMSG_INFO("Built-in iproxy stopped.");
}
