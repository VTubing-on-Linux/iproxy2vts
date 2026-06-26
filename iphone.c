#include "config.h"
#include "iphone.h"
#include "log.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>

static pid_t iproxy_pid = -1;
static int iproxy_local_port = -1;
static int iproxy_remote_port = -1;

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

// will be eventually rewritten to use libimobiledevice directly
void start_iproxy(void) {
    if (iproxy_pid > 0) {
        if (iproxy_local_port == iphone_port && iproxy_remote_port == iphone_port) {
            return;
        }

        stop_iproxy();
    }

    LOGMSG_INFO("Launching iproxy...");
    iproxy_pid = fork();
    if (iproxy_pid == 0) {
        int null = open("/dev/null", O_WRONLY);
        dup2(null, STDOUT_FILENO);
        dup2(null, STDERR_FILENO);
        close(null);
        char local_port[16];
        char remote_port[16];
        snprintf(local_port, sizeof(local_port), "%d", iphone_port);
        snprintf(remote_port, sizeof(remote_port), "%d", iphone_port);
        execlp("iproxy", "iproxy", local_port, remote_port, NULL);
        _exit(1);
    }
    usleep(500000); // Give it 0.5s to bind

    if (iproxy_pid > 0) {
        iproxy_local_port = iphone_port;
        iproxy_remote_port = iphone_port;
    }
}

void stop_iproxy(void) {
    if (iproxy_pid > 0) {
        kill(iproxy_pid, SIGTERM);
        waitpid(iproxy_pid, NULL, 0);
        iproxy_pid = -1;
        iproxy_local_port = -1;
        iproxy_remote_port = -1;
    }
}
