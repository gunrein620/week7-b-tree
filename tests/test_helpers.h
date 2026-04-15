#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define TH_UNUSED __attribute__((unused))
#else
#define TH_UNUSED
#endif

static char g_test_failure_reason[256];

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef _WIN32
typedef struct _stat th_stat_t;
#define TH_LSTAT(path, st) _stat((path), (st))
#define TH_MKDIR(path) _mkdir(path)
#define TH_RMDIR(path) _rmdir(path)
#define TH_GETPID() _getpid()
#define TH_ISDIR(mode) (((mode) & _S_IFDIR) != 0)
#else
typedef struct stat th_stat_t;
#define TH_LSTAT(path, st) lstat((path), (st))
#define TH_MKDIR(path) mkdir((path), 0777)
#define TH_RMDIR(path) rmdir(path)
#define TH_GETPID() getpid()
#define TH_ISDIR(mode) S_ISDIR(mode)
#endif

static int th_fail(const char *reason) {
    strncpy(g_test_failure_reason, reason, sizeof(g_test_failure_reason) - 1);
    g_test_failure_reason[sizeof(g_test_failure_reason) - 1] = '\0';
    return 0;
}

static void th_reset_reason(void) {
    g_test_failure_reason[0] = '\0';
}

static const char *th_reason(void) {
    return g_test_failure_reason[0] == '\0' ? "unknown reason" : g_test_failure_reason;
}

static void th_print_result(const char *name, int ok) {
    if (ok) {
        printf("[PASS] %s\n", name);
    } else {
        printf("[FAIL] %s: %s\n", name, th_reason());
    }
}

static TH_UNUSED void th_join_path(char *buffer,
                                   size_t size,
                                   const char *base,
                                   const char *leaf) {
    snprintf(buffer, size, "%s/%s", base, leaf);
}

static TH_UNUSED void th_remove_tree(const char *path) {
    th_stat_t st;

    if (TH_LSTAT(path, &st) != 0) {
        return;
    }

    if (TH_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        struct dirent *entry;

        if (dir == NULL) {
            return;
        }

        while ((entry = readdir(dir)) != NULL) {
            char child[PATH_MAX];

            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            th_join_path(child, sizeof(child), path, entry->d_name);
            th_remove_tree(child);
        }

        closedir(dir);
        TH_RMDIR(path);
    } else {
        remove(path);
    }
}

static TH_UNUSED int th_write_text_file(const char *path, const char *contents) {
    FILE *file = fopen(path, "w");

    if (file == NULL) {
        return 0;
    }

    fputs(contents, file);
    fclose(file);
    return 1;
}

static TH_UNUSED char *th_read_text_file(const char *path) {
    FILE *file;
    long size;
    char *buffer;

    file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    buffer = (char *)malloc((size_t)size + 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    if (fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[size] = '\0';
    fclose(file);
    return buffer;
}

static TH_UNUSED const char *th_temp_root(void) {
    const char *root = getenv("TMPDIR");

    if (root == NULL || root[0] == '\0') {
        root = getenv("TEMP");
    }
    if (root == NULL || root[0] == '\0') {
        root = getenv("TMP");
    }
    if (root == NULL || root[0] == '\0') {
#ifdef _WIN32
        root = ".";
#else
        root = "/tmp";
#endif
    }

    return root;
}

static TH_UNUSED int th_setup_workspace(const char *name, char *workspace, size_t size) {
    const char *temp_root = th_temp_root();
    char data_dir[PATH_MAX];
    char schema_dir[PATH_MAX];
    char sql_dir[PATH_MAX];

    snprintf(workspace, size, "%s/sqlengine_%s_%ld", temp_root, name, (long)TH_GETPID());
    th_remove_tree(workspace);

    if (TH_MKDIR(workspace) != 0) {
        return 0;
    }

    th_join_path(data_dir, sizeof(data_dir), workspace, "data");
    th_join_path(schema_dir, sizeof(schema_dir), workspace, "schemas");
    th_join_path(sql_dir, sizeof(sql_dir), workspace, "sql");

    if (TH_MKDIR(data_dir) != 0) {
        return 0;
    }
    if (TH_MKDIR(schema_dir) != 0) {
        return 0;
    }
    if (TH_MKDIR(sql_dir) != 0) {
        return 0;
    }

    return 1;
}

static TH_UNUSED int th_write_members_schema(const char *schema_dir) {
    char path[PATH_MAX];

    th_join_path(path, sizeof(path), schema_dir, "members.schema");
    return th_write_text_file(path,
                              "# column_name,type,max_length,nullable,primary_key\n"
                              "id,INT,0,0,1\n"
                              "name,VARCHAR,32,0,0\n"
                              "grade,VARCHAR,16,1,0\n"
                              "class,VARCHAR,16,1,0\n"
                              "age,INT,0,1,0\n");
}

static TH_UNUSED int th_string_contains(const char *haystack, const char *needle) {
    return haystack != NULL && needle != NULL && strstr(haystack, needle) != NULL;
}

#endif
