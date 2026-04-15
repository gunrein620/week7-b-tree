#include "config.h"

#include <string.h>

char g_data_dir[MAX_PATH_LEN] = "./data";
char g_schema_dir[MAX_PATH_LEN] = "./schemas";

static void set_path_value(char *target, const char *path) {
    if (path == NULL || path[0] == '\0') {
        return;
    }

    strncpy(target, path, MAX_PATH_LEN - 1);
    target[MAX_PATH_LEN - 1] = '\0';
}

void config_set_data_dir(const char *path) {
    set_path_value(g_data_dir, path);
}

void config_set_schema_dir(const char *path) {
    set_path_value(g_schema_dir, path);
}
