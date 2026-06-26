#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>
#include <pthread.h>

#include "config.h"
#include "log.h"
#include "notify.h"
#include "iphone.h"
#include "service.h"

#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>

// global flags
int daemon_mode = 0;
volatile int running = 1;
volatile int vts_ready = 0;
volatile int vts_data_detected = 0;

static void on_open(GtkMenuItem *item, gpointer data) { g_print("open\n"); }

static void show_port_config_dialog(void) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Port Config",
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",
        GTK_RESPONSE_CANCEL,
        "_Save",
        GTK_RESPONSE_ACCEPT,
        NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *grid = gtk_grid_new();
    GtkWidget *iphone_label = gtk_label_new("iPhone port:");
    GtkWidget *vts_label = gtk_label_new("VTS server port:");
    GtkWidget *iphone_spin = gtk_spin_button_new_with_range(1, 65535, 1);
    GtkWidget *vts_spin = gtk_spin_button_new_with_range(1, 65535, 1);

    gtk_widget_set_margin_top(grid, 12);
    gtk_widget_set_margin_bottom(grid, 12);
    gtk_widget_set_margin_start(grid, 12);
    gtk_widget_set_margin_end(grid, 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);

    gtk_label_set_xalign(GTK_LABEL(iphone_label), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(vts_label), 0.0f);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(iphone_spin), iphone_port);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(vts_spin), vts_server_port);

    gtk_grid_attach(GTK_GRID(grid), iphone_label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), iphone_spin, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), vts_label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), vts_spin, 1, 1, 1, 1);

    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        int new_iphone_port = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(iphone_spin));
        int new_vts_server_port = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(vts_spin));

        if (save_config(new_iphone_port, new_vts_server_port) == 0) {
            iphone_port = new_iphone_port;
            vts_server_port = new_vts_server_port;
            stop_iproxy();
            LOGMSG_INFO("Saved ports: iPhone=%d VTS=%d", iphone_port, vts_server_port);
        } else {
            GtkWidget *error = gtk_message_dialog_new(
                NULL,
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_ERROR,
                GTK_BUTTONS_CLOSE,
                "Failed to save port settings.");
            gtk_dialog_run(GTK_DIALOG(error));
            gtk_widget_destroy(error);
        }
    }

    gtk_widget_destroy(dialog);
}

static void on_connect(GtkMenuItem *item, gpointer data) {
    (void)item;
    (void)data;
    show_port_config_dialog();
}

static void on_quit(GtkMenuItem *item, gpointer data) { 
    running = 0;
    gtk_main_quit();
}

GtkWidget *statusVTS;
GtkWidget *statusiProxy;

static gboolean update_status_items_cb(gpointer data) {
    (void)data;

    if (vts_ready) {
        gtk_menu_item_set_label(GTK_MENU_ITEM(statusVTS), "VTS is connected");
    } else {
        gtk_menu_item_set_label(GTK_MENU_ITEM(statusVTS), "VTS is not connected");
    }

    if (iphone_connected_and_paired()) {
        gtk_menu_item_set_label(GTK_MENU_ITEM(statusiProxy), "iPhone is connected");
    } else {
        gtk_menu_item_set_label(GTK_MENU_ITEM(statusiProxy), "iPhone is not connected");
    }

    return G_SOURCE_REMOVE;
}

void update_status_items(void) {
    if (!statusVTS || !statusiProxy) {
        return;
    }

    g_main_context_invoke(NULL, update_status_items_cb, NULL);
}

static GtkWidget *build_menu(void) {
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *item;

    item = gtk_menu_item_new_with_label("iProxy2VTS is running");
    gtk_widget_set_sensitive(item, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    statusVTS = gtk_menu_item_new_with_label("VTS is not connected");
    gtk_widget_set_sensitive(statusVTS, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), statusVTS);

    statusiProxy = gtk_menu_item_new_with_label("iPhone is not connected");
    gtk_widget_set_sensitive(statusiProxy, FALSE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), statusiProxy);

    item = gtk_menu_item_new_with_label("Port Config");
    g_signal_connect(item, "activate", G_CALLBACK(on_connect), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(item, "activate", G_CALLBACK(on_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_widget_show_all(menu);
    return menu;
}

static void *service_loop_thread(void *data) {
    (void)data;
    service_loop();
    return NULL;
}




static void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        LOGMSG_INFO("Received signal %d, shutting down...", sig);
        gtk_main_quit();
        running = 0;
    }
}

static void print_usage(const char *progname) {
    printf("Usage: %s [OPTIONS]\n", progname);
    printf("\nOptions:\n");
    printf("  -d, --daemon     Run as a background daemon\n");
    printf("  -f, --foreground Run in foreground (default)\n");
    printf("  -h, --help       Show this help message\n");
    printf("\nDescription:\n");
    printf("  iproxy2vts bridges VTS tracking data between the iPhone app and VTS on the PC.\n");
    printf("\n");
    printf("\nFor systemd service, use: systemctl --user start iproxy2vts\n");
}

static int daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid > 0) {
        _exit(0);
    }

    // child becomes daemon
    setsid();
    
    // ios mindset
    pid = fork();
    if (pid < 0) _exit(1);
    if (pid > 0) _exit(0);

    if (chdir("/") != 0) {
        perror("chdir");
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    (void)open("/dev/null", O_RDONLY);
    (void)open("/dev/null", O_WRONLY);
    (void)open("/dev/null", O_WRONLY);

    openlog("iproxy2vts", LOG_PID, LOG_DAEMON);
    
    return 0;
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--daemon") == 0) {
            daemon_mode = 1;
        } else if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--foreground") == 0) {
            daemon_mode = 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    if (daemon_mode) {
        if (daemonize() < 0) {
            return 1;
        }
    } else {
        setvbuf(stdout, NULL, _IONBF, 0);
    }

    load_config();

    LOGMSG_INFO("iproxy2vts service starting with iPhone port %d and VTS port %d...", iphone_port, vts_server_port);
    send_notification("iproxy2vts Started", "Monitoring for iPhone connection...", "low");

    gtk_init(&argc, &argv);

    AppIndicator *ind = app_indicator_new(
        "iProxy2VTS",
        "preferences-desktop-wallpaper",   // for now its ok
        APP_INDICATOR_CATEGORY_APPLICATION_STATUS);

    app_indicator_set_status(ind, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_menu(ind, GTK_MENU(build_menu()));
    update_status_items();

    pthread_t service_thread;
    if (pthread_create(&service_thread, NULL, service_loop_thread, NULL) != 0) {
        LOGMSG_ERR("Failed to start service loop thread");
        return 1;
    }

    gtk_main();

    running = 0;
    pthread_join(service_thread, NULL);

    stop_iproxy();
    
    if (daemon_mode) {
        closelog();
    }

    LOGMSG_INFO("iproxy2vts service stopped.");
    return 0;
}
