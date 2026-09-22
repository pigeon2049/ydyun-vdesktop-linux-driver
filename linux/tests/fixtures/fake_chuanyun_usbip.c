#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int started;
static int jwae_started;

int cdp_usbip_jwae_set_token(const char *token)
{
    return token != NULL && *token != '\0' ? 0 : -1;
}

int cdp_usbip_start_jwae_service(int auth_type, int mode, const char *server_ip,
                                 int server_port, const char *path, const char *log_path)
{
    (void)path;
    (void)log_path;
    if (auth_type < 0 || mode < 0 || server_ip == NULL || *server_ip == '\0' ||
        server_port < 1 || server_port > 65535) {
        return -1;
    }
    jwae_started = 1;
    return 0;
}

int cdp_usbip_stop_jwae_service(void)
{
    jwae_started = 0;
    return 0;
}

int cdp_usbip_start_service(const char *json, void (*on_init)(int))
{
    if (json == NULL || on_init != NULL) {
        return -1;
    }
    started = 1;
    return 0;
}

int cdp_usbip_stop_service(void)
{
    started = 0;
    return 0;
}

int cdpusblib_get_device_list(void **out_list, int *out_count)
{
    unsigned char *record;

    if (out_list == NULL || out_count == NULL) {
        return -1;
    }
    record = calloc(1U, 0x494U);
    if (record == NULL) {
        return -1;
    }
    *(uint16_t *)(record + 0x00U) = 0x1234U;
    *(uint16_t *)(record + 0x02U) = 0xabcdU;
    memcpy(record + 0x04U, "fake keyboard", 13U);
    memcpy(record + 0x44U, "1-2", 4U);
    *(uint32_t *)(record + 0x84U) = 3U;
    *out_list = record;
    *out_count = 1;
    return 0;
}

void cdpusblib_free_device_list(void *list)
{
    free(list);
}

int cdpusblib_attach_device(const char *busid)
{
    return started && busid != NULL && strcmp(busid, "1-2") == 0 ? 0 : -1;
}

int cdpusblib_unattach_device(const char *busid)
{
    return started && busid != NULL && strcmp(busid, "1-2") == 0 ? 0 : -1;
}

int cdp_usbip_drive_map(const char *busid)
{
    return started && busid != NULL && strcmp(busid, "1-2") == 0 ? 0 : -1;
}

int cdp_usbip_drive_unmap(const char *busid)
{
    return started && busid != NULL && strcmp(busid, "1-2") == 0 ? 0 : -1;
}
