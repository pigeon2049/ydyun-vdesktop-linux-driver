/*
 * Minimal standard-SPICE display/input harness.
 *
 * This is the optional standard-SPICE viewer shipped in the Debian package.
 * It does not implement the China Mobile JWAE/SCG tunnel, login, vendor
 * configuration, or telemetry; those remain outside the clean data plane.
 */

#define _GNU_SOURCE

#include <ctype.h>
#include <gtk/gtk.h>
#include <spice-client-gtk.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SpiceSession *session;
    GtkWidget *display_box;
} Viewer;

typedef struct {
    char *host;
    char port[6];
} Endpoint;

static void free_endpoint(Endpoint *endpoint)
{
    free(endpoint->host);
    endpoint->host = NULL;
    endpoint->port[0] = '\0';
}

static int parse_port_text(const char *text, size_t length, char output[6])
{
    unsigned long value = 0;
    size_t index;

    if (length == 0 || length > 5) {
        return -1;
    }
    for (index = 0; index < length; index++) {
        if (!isdigit((unsigned char)text[index])) {
            return -1;
        }
        value = value * 10U + (unsigned long)(text[index] - '0');
        if (value > 65535U) {
            return -1;
        }
    }
    if (value == 0U) {
        return -1;
    }
    memcpy(output, text, length);
    output[length] = '\0';
    return 0;
}

static int valid_host_text(const char *host, size_t length)
{
    size_t index;

    if (length == 0 || length > 253) {
        return -1;
    }
    for (index = 0; index < length; index++) {
        unsigned char character = (unsigned char)host[index];
        if (!(isalnum(character) || character == '.' || character == '-' ||
              character == ':')) {
            return -1;
        }
    }
    return 0;
}

static int parse_endpoint(const char *value, size_t length, Endpoint *endpoint)
{
    const char *port_start;
    const char *host_start = value;
    const char *closing_bracket;
    size_t host_length;
    size_t port_length;
    size_t index;

    if (length == 0 || length > 1024) {
        return -1;
    }
    if (value[0] == '[') {
        closing_bracket = memchr(value, ']', length);
        if (closing_bracket == NULL ||
            (size_t)(closing_bracket - value + 1) >= length ||
            closing_bracket[1] != ':') {
            return -1;
        }
        host_start = value + 1;
        host_length = (size_t)(closing_bracket - host_start);
        port_start = closing_bracket + 2;
        port_length = length - (size_t)(port_start - value);
    } else {
        port_start = NULL;
        for (index = length; index > 0; index--) {
            if (value[index - 1] == ':') {
                port_start = value + index;
                host_length = index - 1;
                break;
            }
        }
        if (port_start == NULL || host_length == 0 ||
            memchr(value, ':', host_length) != NULL) {
            return -1;
        }
        port_length = length - (size_t)(port_start - value);
    }
    if (valid_host_text(host_start, host_length) != 0 ||
        parse_port_text(port_start, port_length, endpoint->port) != 0) {
        return -1;
    }
    endpoint->host = strndup(host_start, host_length);
    return endpoint->host == NULL ? -1 : 0;
}

static int parse_viewer_url(const char *url, Endpoint *endpoint)
{
    static const char spice_prefix[] = "spice://";
    static const char spice_conn_prefix[] = "spice-conn://";
    const char *payload;
    const char *plus;
    size_t prefix_length;
    size_t length;
    size_t endpoint_length;
    size_t index;

    if (url == NULL) {
        return -1;
    }
    length = strlen(url);
    if (length == 0 || length > 16U * 1024U) {
        return -1;
    }
    for (index = 0; index < length; index++) {
        if (iscntrl((unsigned char)url[index]) || isspace((unsigned char)url[index])) {
            return -1;
        }
    }
    if (strncmp(url, spice_prefix, sizeof(spice_prefix) - 1U) == 0) {
        prefix_length = sizeof(spice_prefix) - 1U;
    } else if (strncmp(url, spice_conn_prefix, sizeof(spice_conn_prefix) - 1U) == 0) {
        prefix_length = sizeof(spice_conn_prefix) - 1U;
    } else {
        return -1;
    }
    payload = url + prefix_length;
    plus = strchr(payload, '+');
    endpoint_length = plus == NULL ? strlen(payload) : (size_t)(plus - payload);
    return parse_endpoint(payload, endpoint_length, endpoint);
}

static void on_channel_event(SpiceChannel *channel,
                             SpiceChannelEvent event,
                             gpointer user_data)
{
    (void)user_data;
    gint type = -1;
    gint id = -1;
    g_object_get(channel, "channel-type", &type, "channel-id", &id, NULL);
    g_message("SPICE channel type=%d id=%d event=%d", type, id, event);
    if (event >= SPICE_CHANNEL_ERROR_CONNECT) {
        gtk_main_quit();
    }
}

static void on_channel_new(SpiceSession *session,
                           SpiceChannel *channel,
                           gpointer user_data)
{
    Viewer *viewer = user_data;
    gint type = -1;
    gint id = -1;
    g_object_get(channel, "channel-type", &type, "channel-id", &id, NULL);
    g_signal_connect(channel, "channel-event", G_CALLBACK(on_channel_event), NULL);

    if (type != SPICE_CHANNEL_DISPLAY) {
        return;
    }

    GtkWidget *display = GTK_WIDGET(spice_display_new(session, id));
    gtk_box_pack_start(GTK_BOX(viewer->display_box), display, TRUE, TRUE, 0);
    gtk_widget_show_all(display);
}

static void on_window_destroy(GtkWidget *window, gpointer user_data)
{
    (void)window;
    Viewer *viewer = user_data;
    spice_session_disconnect(viewer->session);
    gtk_main_quit();
}

int main(int argc, char **argv)
{
    Endpoint endpoint = {0};
    const gchar *host;
    const gchar *port;
    const gchar *tls_port = "";

    if (argc == 2) {
        if (parse_viewer_url(argv[1], &endpoint) != 0) {
            g_printerr("ydyun-spice-viewer: malformed spice viewer URL\n");
            return 2;
        }
        host = endpoint.host;
        port = endpoint.port;
    } else if (argc == 3 || argc == 4) {
        host = argv[1];
        port = argv[2];
        tls_port = argc == 4 ? argv[3] : "";
    } else {
        g_printerr("usage: %s HOST PORT [TLS_PORT]\n", argv[0]);
        g_printerr("       %s spice://HOST:PORT+opaque-session-fields\n", argv[0]);
        return 2;
    }
    if (!gtk_init_check(&argc, &argv)) {
        g_printerr("ydyun-spice-viewer: GTK display is unavailable\n");
        free_endpoint(&endpoint);
        return 2;
    }

    Viewer viewer = {0};
    viewer.session = spice_session_new();
    viewer.display_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "YDYUN standard SPICE viewer harness");
    gtk_window_set_default_size(GTK_WINDOW(window), 1280, 720);
    gtk_container_add(GTK_CONTAINER(window), viewer.display_box);
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), &viewer);
    g_signal_connect(viewer.session, "channel-new", G_CALLBACK(on_channel_new), &viewer);

    g_object_set(viewer.session,
                 "host", host,
                 "port", port,
                 "tls-port", tls_port,
                 NULL);
    gtk_widget_show_all(window);
    if (!spice_session_connect(viewer.session)) {
        g_printerr("ydyun-spice-viewer: standard SPICE connect could not start\n");
        spice_session_disconnect(viewer.session);
        g_object_unref(viewer.session);
        free_endpoint(&endpoint);
        return 1;
    }

    gtk_main();
    g_object_unref(viewer.session);
    free_endpoint(&endpoint);
    return 0;
}
