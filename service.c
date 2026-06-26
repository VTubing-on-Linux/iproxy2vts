#include "service.h"
#include "config.h"
#include "log.h"
#include "notify.h"
#include "iphone.h"
#include "network.h"
#include "bridge.h"

#include <unistd.h>

void service_loop(void) {
    int iphone_was_connected = 0;
    int vts_notification_sent = 0;

    while (running) {
        if (!iphone_connected_and_paired()) {
            if (iphone_was_connected) {
                LOGMSG_WARN("iPhone disconnected");
                send_notification("iPhone Disconnected", "VTS tracking stopped.", "normal");
            }
            iphone_was_connected = 0;
            vts_notification_sent = 0;
            vts_data_detected = 0;
            stop_iproxy();
            update_status_items();
            sleep(IPHONE_POLL_INTERVAL);
            continue;
        }

        if (!iphone_was_connected) {
            LOGMSG_OK("iPhone connected!");
            send_notification("iPhone Connected", "Checking for VTS app...", "low");
            iphone_was_connected = 1;
            update_status_items();
        }

        start_iproxy();
        usleep(500000); // i hate that i have to do this

        if (!vts_data_detected) {
            LOGMSG_INFO("Probing iPhone port %d for VTS data...", iphone_port);
            if (probe_iphone_for_vts_data(PROBE_TIMEOUT)) {
                LOGMSG_OK("VTS data detected from iPhone!");
                vts_data_detected = 1;
                
                if (!vts_notification_sent) {
                    send_notification("VTS Detected on iPhone", 
                        "iPhone is sending VTS data. Waiting for VTS on PC...", "normal");
                    vts_notification_sent = 1;
                }
            } else {
                LOGMSG_INFO("No VTS data detected. VTS app may not be running on iPhone.");
                sleep(2);
                continue;
            }
        }

        if (check_vts_available()) {
            LOGMSG_OK("VTS server is ready! Starting bridge...");
            vts_ready = 1;
            update_status_items();
            run_bridge();
            vts_ready = 0;
            update_status_items();
            // after bridge disconnects, reset and recheck
            vts_data_detected = 0;
            vts_notification_sent = 0;
        } else {
            LOGMSG_INFO("VTS not available on PC. Keeping iPhone connection alive...");
            drain_iphone_data();
            if (vts_ready) {
                run_bridge();
                vts_ready = 0;
                update_status_items();
            }
            vts_data_detected = 0; // proba
        }

        sleep(1);
    }
}
