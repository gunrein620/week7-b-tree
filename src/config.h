#ifndef CONFIG_H
#define CONFIG_H

#include "types.h"

extern char g_data_dir[MAX_PATH_LEN];
extern char g_schema_dir[MAX_PATH_LEN];

void config_set_data_dir(const char *path);
void config_set_schema_dir(const char *path);

#endif
