/*
 * Minimal opt-in loader for the public USB surface shipped by the official
 * UOS/Kylin client.
 *
 * The vendor library is deliberately not linked or packaged.  This helper
 * loads an operator-supplied libchuanyun_usbip.so only when run explicitly.
 * It uses the device-list/attach/detach exports instead of the vendor
 * chuanyun-redirect root daemon, SysV queue, deny-list, udev injection and
 * state-reporting frontend.
 *
 * ABI evidence used here (official libchuanyun_usbip.so, x86_64):
 *   int cdpusblib_get_device_list(void **list, int *count);
 *   void cdpusblib_free_device_list(void *list);
 *   int cdpusblib_attach_device(const char *busid);
 *   int cdpusblib_unattach_device(const char *busid);
 *   int cdp_usbip_drive_map(const char *busid);
 *   int cdp_usbip_drive_unmap(const char *busid);
 *   int cdp_usbip_start_service(const char *json, void (*on_init)(int));
 *   int cdp_usbip_stop_service(void);
 *
 * cdpusblib_get_device_list allocates count records of 0x494 bytes.  The
 * public vendor code fills pid at +0x00, vid at +0x02, name at +0x04,
 * busid at +0x44 and a device-type field at +0x84.  The policy/deny field
 * at +0x88 is intentionally not read or printed here.
 */

#define _POSIX_C_SOURCE 200809L

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVICE_RECORD_SIZE 0x494U
#define DEVICE_NAME_OFFSET 0x04U
#define DEVICE_BUSID_OFFSET 0x44U
#define DEVICE_TYPE_OFFSET 0x84U
#define DEVICE_TEXT_SIZE 64U
#define MAX_DEVICES 4096
#define MAX_JSON_SIZE (1024U * 1024U)
#define MAX_LINE_SIZE 512U

typedef void (*service_init_callback_fn)(int);
typedef int (*start_service_fn)(const char *, service_init_callback_fn);
typedef int (*stop_service_fn)(void);
typedef int (*get_device_list_fn)(void **, int *);
typedef void (*free_device_list_fn)(void *);
typedef int (*attach_device_fn)(const char *);
typedef int (*detach_device_fn)(const char *);
typedef int (*drive_map_fn)(const char *);
typedef int (*drive_unmap_fn)(const char *);
typedef int (*set_token_fn)(const char *);
typedef int (*start_jwae_fn)(int, int, const char *, int, const char *, const char *);
typedef int (*stop_jwae_fn)(void);

typedef struct {
    char *library;
    char *json_file;
    char *token;
    char *server_ip;
    char *path;
    char *log_path;
    int server_port;
    int auth_type;
    int mode;
    int jwae_enabled;
} settings;

typedef struct {
    void *handle;
    start_service_fn start_service;
    stop_service_fn stop_service;
    get_device_list_fn get_device_list;
    free_device_list_fn free_device_list;
    attach_device_fn attach_device;
    detach_device_fn detach_device;
    drive_map_fn drive_map;
    drive_unmap_fn drive_unmap;
    set_token_fn set_token;
    start_jwae_fn start_jwae;
    stop_jwae_fn stop_jwae;
} vendor_api;

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

static int set_string(char **destination, const char *value)
{
    char *copy = strdup(value);

    if (copy == NULL) {
        return -1;
    }
    free(*destination);
    *destination = copy;
    return 0;
}

static int parse_integer(const char *text, int minimum, int maximum, int *result)
{
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *trim(end) != '\0' ||
        value < minimum || value > maximum) {
        return -1;
    }
    *result = (int)value;
    return 0;
}

static int parse_bool(const char *text, int *result)
{
    if (strcmp(text, "1") == 0 || strcmp(text, "yes") == 0 ||
        strcmp(text, "true") == 0 || strcmp(text, "on") == 0) {
        *result = 1;
        return 0;
    }
    if (strcmp(text, "0") == 0 || strcmp(text, "no") == 0 ||
        strcmp(text, "false") == 0 || strcmp(text, "off") == 0) {
        *result = 0;
        return 0;
    }
    return -1;
}

static void free_settings(settings *cfg)
{
    free(cfg->library);
    free(cfg->json_file);
    free(cfg->token);
    free(cfg->server_ip);
    free(cfg->path);
    free(cfg->log_path);
    memset(cfg, 0, sizeof(*cfg));
}

static int load_settings(const char *path, settings *cfg)
{
    FILE *file;
    char line[4096];
    unsigned int line_number = 0;

    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "ydyun-chuanyun-usb: cannot open config: %s\n", path);
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *key;
        char *value;
        char *separator;

        line_number++;
        if (strchr(line, '\n') == NULL && !feof(file)) {
            fprintf(stderr, "ydyun-chuanyun-usb: config line %u is too long\n", line_number);
            fclose(file);
            return -1;
        }
        key = trim(line);
        if (*key == '\0' || *key == '#') {
            continue;
        }
        separator = strchr(key, '=');
        if (separator == NULL) {
            fprintf(stderr, "ydyun-chuanyun-usb: config line %u lacks '='\n", line_number);
            fclose(file);
            return -1;
        }
        *separator = '\0';
        key = trim(key);
        value = trim(separator + 1);
        if (strcmp(key, "library") == 0) {
            if (set_string(&cfg->library, value) != 0) goto oom;
        } else if (strcmp(key, "json_file") == 0) {
            if (set_string(&cfg->json_file, value) != 0) goto oom;
        } else if (strcmp(key, "token") == 0) {
            if (set_string(&cfg->token, value) != 0) goto oom;
        } else if (strcmp(key, "server_ip") == 0) {
            if (set_string(&cfg->server_ip, value) != 0) goto oom;
        } else if (strcmp(key, "path") == 0) {
            if (set_string(&cfg->path, value) != 0) goto oom;
        } else if (strcmp(key, "log_path") == 0) {
            if (set_string(&cfg->log_path, value) != 0) goto oom;
        } else if (strcmp(key, "server_port") == 0) {
            if (parse_integer(value, 1, 65535, &cfg->server_port) != 0) goto bad;
        } else if (strcmp(key, "auth_type") == 0) {
            if (parse_integer(value, 0, 255, &cfg->auth_type) != 0) goto bad;
        } else if (strcmp(key, "mode") == 0) {
            if (parse_integer(value, 0, 255, &cfg->mode) != 0) goto bad;
        } else if (strcmp(key, "jwae_enabled") == 0) {
            if (parse_bool(value, &cfg->jwae_enabled) != 0) goto bad;
        } else {
            fprintf(stderr, "ydyun-chuanyun-usb: unknown config key: %s\n", key);
            fclose(file);
            return -1;
        }
        continue;
bad:
        fprintf(stderr, "ydyun-chuanyun-usb: invalid config line %u\n", line_number);
        fclose(file);
        return -1;
oom:
        fprintf(stderr, "ydyun-chuanyun-usb: out of memory\n");
        fclose(file);
        return -1;
    }
    fclose(file);
    if (cfg->library == NULL) {
        const char *environment = getenv("YDYUN_CHUANYUN_USB_LIB");
        if (environment != NULL && *environment != '\0' &&
            set_string(&cfg->library, environment) != 0) {
            fprintf(stderr, "ydyun-chuanyun-usb: out of memory\n");
            return -1;
        }
    }
    if (cfg->library == NULL) {
        fprintf(stderr, "ydyun-chuanyun-usb: missing library\n");
        return -1;
    }
    if (cfg->jwae_enabled && (cfg->token == NULL || cfg->server_ip == NULL ||
                              cfg->server_port == 0)) {
        fprintf(stderr, "ydyun-chuanyun-usb: jwae_enabled requires token/server_ip/server_port\n");
        return -1;
    }
    return 0;
}

static int load_symbol(void *handle, const char *name, void *destination)
{
    void *symbol = dlsym(handle, name);

    if (symbol == NULL) {
        fprintf(stderr, "ydyun-chuanyun-usb: vendor library lacks %s\n", name);
        return -1;
    }
    *(void **)destination = symbol;
    return 0;
}

static void load_optional_symbol(void *handle, const char *name, void *destination)
{
    void *symbol = dlsym(handle, name);

    if (symbol != NULL) {
        *(void **)destination = symbol;
    }
}

static int load_vendor(const settings *cfg, vendor_api *api)
{
    memset(api, 0, sizeof(*api));
    /*
     * The official USB wrapper is linked against the vendor USB/IP core,
     * which also carries optional video-encoder symbols.  USB-only callers
     * must not resolve that unrelated path at load time.  Lazy resolution is
     * safe here because every required USB entry point is checked with dlsym
     * before use; the vendor library is still supplied explicitly by the
     * operator and is never part of the Debian package.
     */
    api->handle = dlopen(cfg->library, RTLD_LAZY | RTLD_LOCAL);
    if (api->handle == NULL) {
        fprintf(stderr, "ydyun-chuanyun-usb: cannot load supplied library: %s\n", dlerror());
        return -1;
    }
#define LOAD_REQUIRED(field, name) \
    do { if (load_symbol(api->handle, (name), &api->field) != 0) goto fail; } while (0)
    LOAD_REQUIRED(start_service, "cdp_usbip_start_service");
    LOAD_REQUIRED(stop_service, "cdp_usbip_stop_service");
    LOAD_REQUIRED(get_device_list, "cdpusblib_get_device_list");
    LOAD_REQUIRED(free_device_list, "cdpusblib_free_device_list");
    LOAD_REQUIRED(attach_device, "cdpusblib_attach_device");
    LOAD_REQUIRED(detach_device, "cdpusblib_unattach_device");
    load_optional_symbol(api->handle, "cdp_usbip_drive_map", &api->drive_map);
    load_optional_symbol(api->handle, "cdp_usbip_drive_unmap", &api->drive_unmap);
    if (cfg->jwae_enabled) {
        LOAD_REQUIRED(set_token, "cdp_usbip_jwae_set_token");
        LOAD_REQUIRED(start_jwae, "cdp_usbip_start_jwae_service");
        LOAD_REQUIRED(stop_jwae, "cdp_usbip_stop_jwae_service");
    }
#undef LOAD_REQUIRED
    return 0;
fail:
    dlclose(api->handle);
    memset(api, 0, sizeof(*api));
    return -1;
}

static void unload_vendor(vendor_api *api)
{
    if (api->handle != NULL) {
        dlclose(api->handle);
    }
    memset(api, 0, sizeof(*api));
}

static char *read_json(const settings *cfg)
{
    FILE *file;
    char *json;
    size_t length = 0;
    int too_large = 0;

    if (cfg->json_file == NULL) {
        return strdup("");
    }
    file = fopen(cfg->json_file, "rb");
    if (file == NULL) {
        fprintf(stderr, "ydyun-chuanyun-usb: cannot open json_file\n");
        return NULL;
    }
    json = malloc(MAX_JSON_SIZE + 1U);
    if (json == NULL) {
        fclose(file);
        return NULL;
    }
    while (length < MAX_JSON_SIZE) {
        size_t n = fread(json + length, 1U, MAX_JSON_SIZE - length, file);
        length += n;
        if (ferror(file)) {
            fprintf(stderr, "ydyun-chuanyun-usb: cannot read json_file\n");
            free(json);
            fclose(file);
            return NULL;
        }
        if (n == 0U) {
            break;
        }
    }
    if (length == MAX_JSON_SIZE && fgetc(file) != EOF) {
        too_large = 1;
    }
    fclose(file);
    if (too_large) {
        free(json);
        return NULL;
    }
    json[length] = '\0';
    return json;
}

static size_t bounded_text_length(const unsigned char *value, size_t limit)
{
    size_t length = 0;

    while (length < limit && value[length] != '\0') {
        length++;
    }
    return length;
}

static int list_devices(const vendor_api *api)
{
    void *list = NULL;
    int count = 0;
    int result;
    int index;

    result = api->get_device_list(&list, &count);
    if (result != 0) {
        fprintf(stderr, "ydyun-chuanyun-usb: device list failed code=%d\n", result);
        return 1;
    }
    if (count < 0 || count > MAX_DEVICES || (count > 0 && list == NULL)) {
        fprintf(stderr, "ydyun-chuanyun-usb: vendor returned invalid device list\n");
        api->free_device_list(list);
        return 1;
    }
    for (index = 0; index < count; index++) {
        const unsigned char *record = (const unsigned char *)list +
                                      (size_t)index * DEVICE_RECORD_SIZE;
        uint16_t pid;
        uint16_t vid;
        uint32_t device_type;
        size_t name_length;
        size_t busid_length;

        memcpy(&pid, record, sizeof(pid));
        memcpy(&vid, record + 2U, sizeof(vid));
        memcpy(&device_type, record + DEVICE_TYPE_OFFSET, sizeof(device_type));
        name_length = bounded_text_length(record + DEVICE_NAME_OFFSET, DEVICE_TEXT_SIZE);
        busid_length = bounded_text_length(record + DEVICE_BUSID_OFFSET, DEVICE_TEXT_SIZE);
        printf("busid=%.*s vid=%04x pid=%04x type=%u name=%.*s\n",
               (int)busid_length, record + DEVICE_BUSID_OFFSET,
               (unsigned int)vid, (unsigned int)pid, (unsigned int)device_type,
               (int)name_length, record + DEVICE_NAME_OFFSET);
    }
    api->free_device_list(list);
    return 0;
}

static int start_services(const settings *cfg, const vendor_api *api)
{
    char *json;
    int result;

    if (cfg->jwae_enabled) {
        result = api->set_token(cfg->token);
        if (result != 0) {
            fprintf(stderr, "ydyun-chuanyun-usb: token setup failed code=%d\n", result);
            return 1;
        }
        result = api->start_jwae(cfg->auth_type, cfg->mode, cfg->server_ip,
                                 cfg->server_port,
                                 cfg->path != NULL ? cfg->path : "",
                                 cfg->log_path != NULL ? cfg->log_path : "");
        if (result != 0) {
            fprintf(stderr, "ydyun-chuanyun-usb: JWAE start failed code=%d\n", result);
            return 1;
        }
        printf("jwae state=started\n");
    }
    json = read_json(cfg);
    if (json == NULL) {
        if (cfg->jwae_enabled) {
            (void)api->stop_jwae();
        }
        fprintf(stderr, "ydyun-chuanyun-usb: cannot load service configuration\n");
        return 1;
    }
    /* The official ABI takes an optional callback, not an output integer.
     * Passing NULL also matches the official chuanyun-redirect caller and
     * keeps this minimal loader out of the state/monitor callback path. */
    result = api->start_service(json, NULL);
    free(json);
    if (result != 0) {
        fprintf(stderr, "ydyun-chuanyun-usb: USB service start failed code=%d\n", result);
        if (cfg->jwae_enabled) {
            (void)api->stop_jwae();
        }
        return 1;
    }
    printf("usb state=started\n");
    return 0;
}

static void stop_services(const settings *cfg, const vendor_api *api)
{
    (void)api->stop_service();
    if (cfg->jwae_enabled) {
        (void)api->stop_jwae();
    }
}

static int command_loop(const settings *cfg, const vendor_api *api)
{
    char line[MAX_LINE_SIZE];

    (void)cfg;

    while (!stop_requested && fgets(line, sizeof(line), stdin) != NULL) {
        char *command = trim(line);
        char *argument = strchr(command, ' ');
        int result;

        if (argument != NULL) {
            *argument++ = '\0';
            argument = trim(argument);
        }
        if (strcmp(command, "list") == 0) {
            (void)list_devices(api);
        } else if (strcmp(command, "attach") == 0 && argument != NULL && *argument != '\0') {
            result = api->attach_device(argument);
            printf("attach busid=%s code=%d\n", argument, result);
        } else if (strcmp(command, "detach") == 0 && argument != NULL && *argument != '\0') {
            result = api->detach_device(argument);
            printf("detach busid=%s code=%d\n", argument, result);
        } else if (strcmp(command, "map") == 0 && argument != NULL && *argument != '\0') {
            if (api->drive_map == NULL) {
                fprintf(stderr, "ydyun-chuanyun-usb: vendor library lacks drive-map ABI\n");
            } else {
                result = api->drive_map(argument);
                printf("map busid=%s code=%d\n", argument, result);
            }
        } else if (strcmp(command, "unmap") == 0 && argument != NULL && *argument != '\0') {
            if (api->drive_unmap == NULL) {
                fprintf(stderr, "ydyun-chuanyun-usb: vendor library lacks drive-unmap ABI\n");
            } else {
                result = api->drive_unmap(argument);
                printf("unmap busid=%s code=%d\n", argument, result);
            }
        } else if (strcmp(command, "stop") == 0 || strcmp(command, "quit") == 0) {
            break;
        } else if (*command != '\0') {
            fprintf(stderr, "ydyun-chuanyun-usb: commands are list, attach BUSID, detach BUSID, map BUSID, unmap BUSID, stop\n");
        }
        fflush(stdout);
    }
    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr, "usage: %s CONFIG run|list|attach BUSID|detach BUSID|map BUSID|unmap BUSID\n", program);
    fprintf(stderr, "       run starts the explicit vendor service and reads commands from stdin\n");
}

int main(int argc, char **argv)
{
    settings cfg = {0};
    vendor_api api;
    struct sigaction action;
    int result = 0;
    int started = 0;

    if (argc < 3 || (strcmp(argv[2], "attach") == 0 && argc != 4) ||
        (strcmp(argv[2], "detach") == 0 && argc != 4) ||
        (strcmp(argv[2], "map") == 0 && argc != 4) ||
        (strcmp(argv[2], "unmap") == 0 && argc != 4) ||
        ((strcmp(argv[2], "run") == 0 || strcmp(argv[2], "list") == 0) && argc != 3)) {
        usage(argv[0]);
        return 2;
    }
    if (load_settings(argv[1], &cfg) != 0 || load_vendor(&cfg, &api) != 0) {
        free_settings(&cfg);
        return 2;
    }
    memset(&action, 0, sizeof(action));
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    (void)sigaction(SIGINT, &action, NULL);
    (void)sigaction(SIGTERM, &action, NULL);

    if (strcmp(argv[2], "list") == 0) {
        result = list_devices(&api);
    } else if (strcmp(argv[2], "attach") == 0) {
        result = api.attach_device(argv[3]) == 0 ? 0 : 1;
    } else if (strcmp(argv[2], "detach") == 0) {
        result = api.detach_device(argv[3]) == 0 ? 0 : 1;
    } else if (strcmp(argv[2], "map") == 0) {
        result = api.drive_map != NULL && api.drive_map(argv[3]) == 0 ? 0 : 1;
    } else if (strcmp(argv[2], "unmap") == 0) {
        result = api.drive_unmap != NULL && api.drive_unmap(argv[3]) == 0 ? 0 : 1;
    } else if (strcmp(argv[2], "run") == 0) {
        result = start_services(&cfg, &api);
        if (result == 0) {
            started = 1;
            result = command_loop(&cfg, &api);
        }
    } else {
        usage(argv[0]);
        result = 2;
    }
    if (started) {
        stop_services(&cfg, &api);
    }
    unload_vendor(&api);
    free_settings(&cfg);
    return result;
}
