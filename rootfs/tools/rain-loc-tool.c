/* rain-loc-tool — apply the Qualcomm GNSS configuration that the vendor IZat
 * HAL normally pushes over QMI LOC.  qmicli exposes only a subset of the LOC
 * requests (no --loc-set-server, no XTRA injection), so we call libqmi
 * directly.
 *
 * Build (on the device):
 *   gcc -O2 -o rain-loc-tool rain-loc-tool.c $(pkg-config --cflags --libs qmi-glib)
 *
 * Usage:
 *   rain-loc-tool getopmode
 *   rain-loc-tool mode <standalone|msb|msa|cellid|wwan>
 *   rain-loc-tool server <url-or-ip> [port]
 *   rain-loc-tool xtra <file>
 *   rain-loc-tool orbits <file>
 *   rain-loc-tool start | stop
 */
#include <libqmi-glib.h>
#include <libqrtr-glib.h>
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static QmiDevice *device;
static QmiClientLoc *loc;

typedef struct { gboolean done; GAsyncResult *res; } Op;

static void op_cb (GObject *src, GAsyncResult *res, gpointer ud) {
    Op *o = ud;
    o->res = g_object_ref (res);
    o->done = TRUE;
}

static void op_wait (Op *o) {
    while (!o->done)
        g_main_context_iteration (NULL, TRUE);
}

static int die (const char *what, GError *e) {
    fprintf (stderr, "ERR %s: %s\n", what, e ? e->message : "unknown");
    return 1;
}

static int cmd_getopmode (void)
{
    Op op = {0}; GError *e = NULL; QmiLocOperationMode m;

    qmi_client_loc_get_operation_mode (loc, NULL, 30, NULL, op_cb, &op);
    op_wait (&op);
    QmiMessageLocGetOperationModeOutput *out =
        qmi_client_loc_get_operation_mode_finish (loc, op.res, &e);
    if (!out)
        return die ("get opmode", e);
    if (!qmi_message_loc_get_operation_mode_output_get_result (out, &e)) {
        qmi_message_loc_get_operation_mode_output_unref (out);
        return die ("get opmode", e);
    }
    printf ("operation mode query OK (value not exposed by libqmi)\n");
    qmi_message_loc_get_operation_mode_output_unref (out);
    return 0;
}

static int cmd_mode (const char *s)
{
    Op op = {0}; GError *e = NULL;
    QmiLocOperationMode m = QMI_LOC_OPERATION_MODE_STANDALONE;

    if (!strcmp (s, "msa")) m = QMI_LOC_OPERATION_MODE_MSA;
    else if (!strcmp (s, "msb")) m = QMI_LOC_OPERATION_MODE_MSB;
    else if (!strcmp (s, "cellid")) m = QMI_LOC_OPERATION_MODE_CELLID;
    else if (!strcmp (s, "wwan")) m = QMI_LOC_OPERATION_MODE_WWAN;

    QmiMessageLocSetOperationModeInput *in = qmi_message_loc_set_operation_mode_input_new ();
    qmi_message_loc_set_operation_mode_input_set_operation_mode (in, m, NULL);
    qmi_client_loc_set_operation_mode (loc, in, 30, NULL, op_cb, &op);
    op_wait (&op);
    QmiMessageLocSetOperationModeOutput *out =
        qmi_client_loc_set_operation_mode_finish (loc, op.res, &e);
    qmi_message_loc_set_operation_mode_input_unref (in);
    if (!out)
        return die ("set opmode", e);
    qmi_message_loc_set_operation_mode_output_unref (out);
    printf ("set operation mode %s -> OK\n", s);
    return 0;
}

static int cmd_server (const char *addr, const char *port)
{
    Op op = {0}; GError *e = NULL;
    char buf[256];

    if (port)
        snprintf (buf, sizeof (buf), "%s:%s", addr, port);
    else
        snprintf (buf, sizeof (buf), "%s", addr);

    QmiMessageLocSetServerInput *in = qmi_message_loc_set_server_input_new ();
    qmi_message_loc_set_server_input_set_server_type (in, QMI_LOC_SERVER_TYPE_UMTS_SLP, NULL);
    qmi_message_loc_set_server_input_set_url (in, buf, NULL);
    qmi_client_loc_set_server (loc, in, 30, NULL, op_cb, &op);
    op_wait (&op);
    QmiMessageLocSetServerOutput *out = qmi_client_loc_set_server_finish (loc, op.res, &e);
    qmi_message_loc_set_server_input_unref (in);
    if (!out)
        return die ("set server", e);
    qmi_message_loc_set_server_output_unref (out);
    printf ("set SUPL server %s OK\n", buf);
    return 0;
}

static int read_file (const char *path, unsigned char **buf, long *sz)
{
    FILE *f = fopen (path, "rb");
    if (!f) { fprintf (stderr, "cannot open %s\n", path); return -1; }
    fseek (f, 0, SEEK_END);
    *sz = ftell (f);
    fseek (f, 0, SEEK_SET);
    *buf = malloc (*sz);
    if (!*buf || fread (*buf, 1, *sz, f) != (size_t) *sz) { fclose (f); return -1; }
    fclose (f);
    return 0;
}

static int cmd_xtra (const char *path)
{
    unsigned char *buf = NULL; long sz = 0;
    const int CHUNK = 1024;
    int parts, i;

    if (read_file (path, &buf, &sz) < 0)
        return 1;
    parts = (sz + CHUNK - 1) / CHUNK;
    printf ("xtra: %ld bytes in %d parts\n", sz, parts);

    for (i = 0; i < parts; i++) {
        int len = (i == parts - 1) ? (int) (sz - i * CHUNK) : CHUNK;
        GArray *arr = g_array_sized_new (FALSE, FALSE, sizeof (guint8), len);
        Op op = {0}; GError *e = NULL;

        g_array_append_vals (arr, buf + i * CHUNK, len);
        QmiMessageLocInjectXtraDataInput *in = qmi_message_loc_inject_xtra_data_input_new ();
        qmi_message_loc_inject_xtra_data_input_set_total_size (in, (guint32) sz, NULL);
        qmi_message_loc_inject_xtra_data_input_set_total_parts (in, (guint8) parts, NULL);
        qmi_message_loc_inject_xtra_data_input_set_part_number (in, (guint8) (i + 1), NULL);
        qmi_message_loc_inject_xtra_data_input_set_part_data (in, arr, NULL);
        qmi_client_loc_inject_xtra_data (loc, in, 60, NULL, op_cb, &op);
        op_wait (&op);
        QmiMessageLocInjectXtraDataOutput *out =
            qmi_client_loc_inject_xtra_data_finish (loc, op.res, &e);
        qmi_message_loc_inject_xtra_data_input_unref (in);
        g_array_unref (arr);
        if (!out) {
            fprintf (stderr, "part %d/%d failed: %s\n", i + 1, parts, e ? e->message : "?");
            free (buf);
            return 1;
        }
        qmi_message_loc_inject_xtra_data_output_unref (out);
    }
    printf ("injected %d parts OK\n", parts);
    free (buf);
    return 0;
}

static int cmd_orbits (const char *path)
{
    unsigned char *buf = NULL; long sz = 0;
    const int CHUNK = 1024;
    int parts, i;

    if (read_file (path, &buf, &sz) < 0)
        return 1;
    parts = (sz + CHUNK - 1) / CHUNK;
    printf ("orbits: %ld bytes in %d parts\n", sz, parts);

    for (i = 0; i < parts; i++) {
        int len = (i == parts - 1) ? (int) (sz - i * CHUNK) : CHUNK;
        GArray *arr = g_array_sized_new (FALSE, FALSE, sizeof (guint8), len);
        Op op = {0}; GError *e = NULL;

        g_array_append_vals (arr, buf + i * CHUNK, len);
        QmiMessageLocInjectPredictedOrbitsDataInput *in =
            qmi_message_loc_inject_predicted_orbits_data_input_new ();
        qmi_message_loc_inject_predicted_orbits_data_input_set_format_type (
            in, QMI_LOC_PREDICTED_ORBITS_DATA_FORMAT_XTRA, NULL);
        qmi_message_loc_inject_predicted_orbits_data_input_set_total_size (in, (guint32) sz, NULL);
        qmi_message_loc_inject_predicted_orbits_data_input_set_total_parts (in, (guint8) parts, NULL);
        qmi_message_loc_inject_predicted_orbits_data_input_set_part_number (in, (guint8) (i + 1), NULL);
        qmi_message_loc_inject_predicted_orbits_data_input_set_part_data (in, arr, NULL);
        qmi_client_loc_inject_predicted_orbits_data (loc, in, 60, NULL, op_cb, &op);
        op_wait (&op);
        QmiMessageLocInjectPredictedOrbitsDataOutput *out =
            qmi_client_loc_inject_predicted_orbits_data_finish (loc, op.res, &e);
        qmi_message_loc_inject_predicted_orbits_data_input_unref (in);
        g_array_unref (arr);
        if (!out) {
            fprintf (stderr, "part %d/%d failed: %s\n", i + 1, parts, e ? e->message : "?");
            free (buf);
            return 1;
        }
        qmi_message_loc_inject_predicted_orbits_data_output_unref (out);
    }
    printf ("injected %d parts OK\n", parts);
    free (buf);
    return 0;
}

static int cmd_inject_time (void)
{
    Op op = {0}; GError *e = NULL;
    /* "UTC time" is milliseconds since the GPS epoch (1980-01-06T00:00:00Z);
     * the vendor HAL injects the AP system time this way (gps.conf has
     * XTRA_SYSTEM_TIME_INJECT=1) so the engine can start without decoding
     * the time of week from the satellites. */
    const guint64 EPOCH_1980_MS = 315964800000ULL;
    GTimeVal tv;
    g_get_current_time (&tv);
    guint64 now_ms = (guint64) tv.tv_sec * 1000ULL + (guint64) (tv.tv_usec / 1000);
    guint64 utc_ms = now_ms - EPOCH_1980_MS;

    QmiMessageLocInjectUtcTimeInput *in = qmi_message_loc_inject_utc_time_input_new ();
    qmi_message_loc_inject_utc_time_input_set_utc_time (in, utc_ms, NULL);
    qmi_message_loc_inject_utc_time_input_set_time_uncertainty (in, 10, NULL);
    qmi_message_loc_inject_utc_time_input_set_time_source (
        in, QMI_LOC_TIME_SOURCE_EXTERNAL_INPUT, NULL);
    qmi_client_loc_inject_utc_time (loc, in, 30, NULL, op_cb, &op);
    op_wait (&op);
    QmiMessageLocInjectUtcTimeOutput *out = qmi_client_loc_inject_utc_time_finish (loc, op.res, &e);
    qmi_message_loc_inject_utc_time_input_unref (in);
    if (!out)
        return die ("inject time", e);
    qmi_message_loc_inject_utc_time_output_unref (out);
    printf ("injected UTC time %llu ms (since 1980), uncertainty 10 ms\n",
            (unsigned long long) utc_ms);
    return 0;
}

static int cmd_startstop (gboolean start)
{
    Op op = {0}; GError *e = NULL;

    if (start) {
        QmiMessageLocStartInput *in = qmi_message_loc_start_input_new ();
        qmi_client_loc_start (loc, in, 30, NULL, op_cb, &op);
        op_wait (&op);
        QmiMessageLocStartOutput *out = qmi_client_loc_start_finish (loc, op.res, &e);
        qmi_message_loc_start_input_unref (in);
        if (!out) return die ("start", e);
        qmi_message_loc_start_output_unref (out);
        printf ("session started\n");
    } else {
        QmiMessageLocStopInput *in = qmi_message_loc_stop_input_new ();
        qmi_client_loc_stop (loc, in, 30, NULL, op_cb, &op);
        op_wait (&op);
        QmiMessageLocStopOutput *out = qmi_client_loc_stop_finish (loc, op.res, &e);
        qmi_message_loc_stop_input_unref (in);
        if (!out) return die ("stop", e);
        qmi_message_loc_stop_output_unref (out);
        printf ("session stopped\n");
    }
    return 0;
}

int main (int argc, char **argv)
{
    GError *e = NULL;
    Op op = {0};
    const char *cmd;
    QrtrBus *bus;
    QrtrNode *node;
    int ret;

    if (argc < 2) {
        fprintf (stderr, "usage: %s getopmode|mode|server|xtra|orbits|start|stop [args]\n", argv[0]);
        return 2;
    }
    cmd = argv[1];

    qrtr_bus_new (5000, NULL, op_cb, &op);
    op_wait (&op);
    bus = qrtr_bus_new_finish (op.res, &e);
    if (!bus)
        return die ("qrtr bus", e);

    node = qrtr_bus_get_node (bus, 0);
    if (!node)
        return die ("qrtr node 0", NULL);

    memset (&op, 0, sizeof (op));
    qmi_device_new_from_node (node, NULL, op_cb, &op);
    op_wait (&op);
    device = qmi_device_new_from_node_finish (op.res, &e);
    if (!device)
        return die ("open node", e);

    memset (&op, 0, sizeof (op));
    qmi_device_open (device, QMI_DEVICE_OPEN_FLAGS_NONE, 30, NULL, op_cb, &op);
    op_wait (&op);
    if (!qmi_device_open_finish (device, op.res, &e))
        return die ("open device", e);

    memset (&op, 0, sizeof (op));
    qmi_device_allocate_client (device, QMI_SERVICE_LOC, QMI_CID_NONE, 30, NULL, op_cb, &op);
    op_wait (&op);
    QmiClient *client = qmi_device_allocate_client_finish (device, op.res, &e);
    if (!client)
        return die ("allocate LOC client", e);
    loc = QMI_CLIENT_LOC (client);

    if (!strcmp (cmd, "getopmode"))        ret = cmd_getopmode ();
    else if (!strcmp (cmd, "mode"))        ret = cmd_mode (argc > 2 ? argv[2] : "standalone");
    else if (!strcmp (cmd, "server"))      ret = cmd_server (argc > 2 ? argv[2] : "supl.google.com",
                                                             argc > 3 ? argv[3] : NULL);
    else if (!strcmp (cmd, "xtra"))        ret = cmd_xtra (argc > 2 ? argv[2] : "/tmp/xtra.bin");
    else if (!strcmp (cmd, "orbits"))      ret = cmd_orbits (argc > 2 ? argv[2] : "/tmp/xtra.bin");
    else if (!strcmp (cmd, "time"))        ret = cmd_inject_time ();
    else if (!strcmp (cmd, "start"))       ret = cmd_startstop (TRUE);
    else if (!strcmp (cmd, "stop"))        ret = cmd_startstop (FALSE);
    else { fprintf (stderr, "unknown command %s\n", cmd); ret = 2; }

    g_object_unref (device);
    g_object_unref (bus);
    return ret;
}
