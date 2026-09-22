/*
 * Optional loader for the public Linux Chuanyun SDK boundary.
 *
 * This program deliberately does not link against, ship, or start the
 * vendor libraries.  It loads an operator-supplied libchuanyun.so only when
 * invoked explicitly, passes no log/monitor callback, and does not print
 * auth codes, tokens, error strings, or network measurements.
 *
 * The declarations below mirror the shipped ccsdk/uos/include/chuanyun_api.h
 * ABI.  Keeping the loader isolated lets the normal USB/IP package remain
 * independent of the closed vendor runtime.
 */

#define _POSIX_C_SOURCE 200809L

#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef void (*connect_status_cb)(const char *, int, const char *, const char *, void *);
typedef void (*frame_info_cb)(const char *, void *);

typedef struct {
    const char *server_ip;
    int server_port;
    const char *log_server_ip;
    int log_server_port;
    const char *terminal_sn;
    const char *unit_type;
    connect_status_cb connection;
    frame_info_cb first_frame;
    void *net_info;
    void *vm_event;
} chuanyun_init_param;

typedef int (*chuanyun_init_fn)(chuanyun_init_param *);
typedef int (*connect_vm_fn)(const char *, const char *, const char *, const char *);
typedef int (*disconnect_fn)(const char *);
typedef int (*deinit_fn)(void);

typedef struct {
    char *library;
    char *server_ip;
    int server_port;
    char *terminal_sn;
    char *unit_type;
    char *vm_id;
    char *username;
    char *auth_code;
    char *biz_code;
} settings;

static volatile sig_atomic_t stop_requested;

static void on_signal(int signum)
{
    (void)signum;
    stop_requested = 1;
}

static char *trim(char *value)
{
    char *end;
    while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') {
        value++;
    }
    end = value + strlen(value);
    while (end > value && (end[-1] == ' ' || end[-1] == '\t' ||
                           end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }
    return value;
}

static void free_settings(settings *cfg)
{
    free(cfg->library);
    free(cfg->server_ip);
    free(cfg->terminal_sn);
    free(cfg->unit_type);
    free(cfg->vm_id);
    free(cfg->username);
    free(cfg->auth_code);
    free(cfg->biz_code);
    memset(cfg, 0, sizeof(*cfg));
}

static int set_value(char **destination, const char *value)
{
    char *copy = strdup(value);
    if (copy == NULL) {
        return -1;
    }
    free(*destination);
    *destination = copy;
    return 0;
}

static int parse_port(const char *text, int *port)
{
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *trim(end) != '\0' || value < 1 || value > 65535) {
        return -1;
    }
    *port = (int)value;
    return 0;
}

static int load_settings(const char *path, settings *cfg)
{
    FILE *file = fopen(path, "r");
    char line[4096];
    unsigned int line_number = 0;

    if (file == NULL) {
        fprintf(stderr, "ydyun-chuanyun-session: cannot open config: %s\n", path);
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *key;
        char *value;
        char *separator;
        line_number++;
        if (strchr(line, '\n') == NULL && !feof(file)) {
            fprintf(stderr, "ydyun-chuanyun-session: config line %u is too long\n", line_number);
            fclose(file);
            return -1;
        }
        key = trim(line);
        if (*key == '\0' || *key == '#') {
            continue;
        }
        separator = strchr(key, '=');
        if (separator == NULL) {
            fprintf(stderr, "ydyun-chuanyun-session: config line %u lacks '='\n", line_number);
            fclose(file);
            return -1;
        }
        *separator = '\0';
        value = trim(separator + 1);
        key = trim(key);
        if (strcmp(key, "library") == 0) {
            if (set_value(&cfg->library, value) != 0) goto oom;
        } else if (strcmp(key, "server_ip") == 0) {
            if (set_value(&cfg->server_ip, value) != 0) goto oom;
        } else if (strcmp(key, "server_port") == 0) {
            if (parse_port(value, &cfg->server_port) != 0) goto bad;
        } else if (strcmp(key, "terminal_sn") == 0) {
            if (set_value(&cfg->terminal_sn, value) != 0) goto oom;
        } else if (strcmp(key, "unit_type") == 0) {
            if (set_value(&cfg->unit_type, value) != 0) goto oom;
        } else if (strcmp(key, "vm_id") == 0) {
            if (set_value(&cfg->vm_id, value) != 0) goto oom;
        } else if (strcmp(key, "username") == 0) {
            if (set_value(&cfg->username, value) != 0) goto oom;
        } else if (strcmp(key, "auth_code") == 0) {
            if (set_value(&cfg->auth_code, value) != 0) goto oom;
        } else if (strcmp(key, "biz_code") == 0) {
            if (set_value(&cfg->biz_code, value) != 0) goto oom;
        } else {
            fprintf(stderr, "ydyun-chuanyun-session: unknown config key: %s\n", key);
            fclose(file);
            return -1;
        }
        continue;
bad:
        fprintf(stderr, "ydyun-chuanyun-session: invalid config line %u\n", line_number);
        fclose(file);
        return -1;
oom:
        fprintf(stderr, "ydyun-chuanyun-session: out of memory\n");
        fclose(file);
        return -1;
    }
    fclose(file);
    if (cfg->library == NULL) {
        const char *environment = getenv("YDYUN_CHUANYUN_LIB");
        if (environment != NULL && *environment != '\0' && set_value(&cfg->library, environment) != 0) {
            fprintf(stderr, "ydyun-chuanyun-session: out of memory\n");
            return -1;
        }
    }
    if (cfg->library == NULL || cfg->server_ip == NULL || cfg->server_port == 0 ||
        cfg->vm_id == NULL || cfg->username == NULL || cfg->auth_code == NULL ||
        cfg->biz_code == NULL) {
        fprintf(stderr, "ydyun-chuanyun-session: missing required config field\n");
        return -1;
    }
    return 0;
}

static void connection_callback(const char *vm_id, int code, const char *error_code,
                                const char *error_message, void *user_data)
{
    (void)error_code;
    (void)error_message;
    (void)user_data;
    if (code == 0) {
        printf("connection vm=%s state=connected\n", vm_id != NULL ? vm_id : "(unknown)");
    } else {
        printf("connection vm=%s state=failed code=%d\n", vm_id != NULL ? vm_id : "(unknown)", code);
    }
    fflush(stdout);
}

static void first_frame_callback(const char *vm_id, void *user_data)
{
    (void)user_data;
    printf("display vm=%s state=first-frame\n", vm_id != NULL ? vm_id : "(unknown)");
    fflush(stdout);
}

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s CONFIG\n", program);
    fprintf(stderr, "       config is an operator-supplied key=value file; no login is performed\n");
}

int main(int argc, char **argv)
{
    settings cfg = {0};
    void *library = NULL;
    chuanyun_init_fn init_api;
    connect_vm_fn connect_api;
    disconnect_fn disconnect_api;
    deinit_fn deinit_api;
    chuanyun_init_param init_param;
    int result;

    if (argc != 2) {
        usage(argv[0]);
        return 2;
    }
    if (load_settings(argv[1], &cfg) != 0) {
        free_settings(&cfg);
        return 2;
    }
    library = dlopen(cfg.library, RTLD_NOW | RTLD_LOCAL);
    if (library == NULL) {
        fprintf(stderr, "ydyun-chuanyun-session: cannot load supplied vendor library: %s\n", dlerror());
        free_settings(&cfg);
        return 1;
    }
    *(void **)(&init_api) = dlsym(library, "chuanyun_init");
    *(void **)(&connect_api) = dlsym(library, "connectVm");
    *(void **)(&disconnect_api) = dlsym(library, "disconnect");
    *(void **)(&deinit_api) = dlsym(library, "deInit");
    if (init_api == NULL || connect_api == NULL || disconnect_api == NULL || deinit_api == NULL) {
        fprintf(stderr, "ydyun-chuanyun-session: supplied library lacks the public Chuanyun ABI\n");
        dlclose(library);
        free_settings(&cfg);
        return 1;
    }

    memset(&init_param, 0, sizeof(init_param));
    init_param.server_ip = cfg.server_ip;
    init_param.server_port = cfg.server_port;
    init_param.terminal_sn = cfg.terminal_sn;
    init_param.unit_type = cfg.unit_type;
    init_param.connection = connection_callback;
    init_param.first_frame = first_frame_callback;
    result = init_api(&init_param);
    if (result != 0) {
        fprintf(stderr, "ydyun-chuanyun-session: vendor init failed code=%d\n", result);
        dlclose(library);
        free_settings(&cfg);
        return 1;
    }
    result = connect_api(cfg.vm_id, cfg.username, cfg.auth_code, cfg.biz_code);
    if (result == 0) {
        printf("connection request submitted vm=%s\n", cfg.vm_id);
        fflush(stdout);
    } else {
        fprintf(stderr, "ydyun-chuanyun-session: connection request failed code=%d\n", result);
        deinit_api();
        dlclose(library);
        free_settings(&cfg);
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    while (!stop_requested) {
        pause();
    }
    (void)disconnect_api(cfg.vm_id);
    (void)deinit_api();
    dlclose(library);
    free_settings(&cfg);
    return 0;
}
