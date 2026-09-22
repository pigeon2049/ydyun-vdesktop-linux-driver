#include <stddef.h>

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
} init_param;

static init_param saved;

int chuanyun_init(init_param *param)
{
    if (param == NULL || param->connection == NULL || param->first_frame == NULL) {
        return 22;
    }
    saved = *param;
    return 0;
}

int connectVm(const char *vmid, const char *username, const char *auth_code,
              const char *biz_code)
{
    (void)username;
    (void)auth_code;
    (void)biz_code;
    if (saved.connection == NULL || saved.first_frame == NULL || vmid == NULL) {
        return 22;
    }
    saved.connection(vmid, 0, "", "", NULL);
    saved.first_frame(vmid, NULL);
    return 0;
}

int disconnect(const char *vmid)
{
    (void)vmid;
    return 0;
}

int deInit(void)
{
    saved.connection = NULL;
    saved.first_frame = NULL;
    return 0;
}
