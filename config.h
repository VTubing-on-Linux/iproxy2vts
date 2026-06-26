#ifndef CONFIG_H
#define CONFIG_H

// Port configuration
#define DEFAULT_IPHONE_PORT 25561    // VTS on iPhone
#define DEFAULT_VTS_SERVER_PORT 25565 // VTS on PC

extern int iphone_port;
extern int vts_server_port;

void load_config(void);
int save_config(int new_iphone_port, int new_vts_server_port);

// Buffer sizes
#define BUF_SIZE 8192
#define MAX_EVENTS 16

// Timing configuration (in seconds)
#define VTS_CHECK_INTERVAL 2        // Seconds between VTS availability checks
#define IPHONE_POLL_INTERVAL 1      // Seconds between iPhone connection checks
#define PROBE_TIMEOUT 5             // Seconds to wait for data when probing
#define KEEPALIVE_INTERVAL 30       // Seconds between keepalive checks

#endif // CONFIG_H
