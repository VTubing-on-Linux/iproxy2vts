#include "config.h"
#include "log.h"

#include <glib.h>

int iphone_port = DEFAULT_IPHONE_PORT;
int vts_server_port = DEFAULT_VTS_SERVER_PORT;

static char *get_config_dir(void) {
    return g_build_filename(g_get_user_config_dir(), "iproxy2vts", NULL);
}

static char *get_config_path(void) {
    char *dir = get_config_dir();
    char *path = g_build_filename(dir, "config.ini", NULL);
    g_free(dir);
    return path;
}

static gboolean valid_port(int port) {
    return port > 0 && port <= 65535;
}

void load_config(void) {
    g_autofree char *path = get_config_path();
    g_autoptr(GKeyFile) key_file = g_key_file_new();

    if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, NULL)) {
        return;
    }

    if (g_key_file_has_key(key_file, "ports", "iphone_port", NULL)) {
        int value = g_key_file_get_integer(key_file, "ports", "iphone_port", NULL);
        if (valid_port(value)) {
            iphone_port = value;
        }
    }

    if (g_key_file_has_key(key_file, "ports", "vts_server_port", NULL)) {
        int value = g_key_file_get_integer(key_file, "ports", "vts_server_port", NULL);
        if (valid_port(value)) {
            vts_server_port = value;
        }
    }
}

int save_config(int new_iphone_port, int new_vts_server_port) {
    if (!valid_port(new_iphone_port) || !valid_port(new_vts_server_port)) {
        LOGMSG_ERR("Refusing to save invalid ports: iphone=%d vts=%d", new_iphone_port, new_vts_server_port);
        return -1;
    }

    g_autofree char *dir = get_config_dir();
    g_autofree char *path = get_config_path();
    g_autoptr(GKeyFile) key_file = g_key_file_new();

    g_key_file_set_integer(key_file, "ports", "iphone_port", new_iphone_port);
    g_key_file_set_integer(key_file, "ports", "vts_server_port", new_vts_server_port);

    if (g_mkdir_with_parents(dir, 0700) != 0) {
        LOGMSG_ERR("Failed to create config directory: %s", dir);
        return -1;
    }

    gsize length = 0;
    g_autoptr(GError) error = NULL;
    g_autofree char *data = g_key_file_to_data(key_file, &length, &error);
    if (!data) {
        LOGMSG_ERR("Failed to serialize config: %s", error ? error->message : "unknown error");
        return -1;
    }

    if (!g_file_set_contents(path, data, (gssize)length, &error)) {
        LOGMSG_ERR("Failed to save config to %s: %s", path, error ? error->message : "unknown error");
        return -1;
    }

    return 0;
}