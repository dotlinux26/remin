// vte_snapshot_consumer_test.cpp — External consumer proof
//
// Uses ONLY the public VTE API (<vte/vte.h>) + public Gtk API — no VTE internal
// headers. Proves the snapshot patch provides a usable dependency outside the
// VTE source tree (consumer model identical to a Remin TerminalPane).
//
// Build (against a patched VTE build):
//   PKG_CONFIG_PATH=<vte-build>/meson-uninstalled:$PKG_CONFIG_PATH
//   g++ -std=c++20 vte_snapshot_consumer_test.cpp -o consumer_test \
//       $(pkg-config --cflags vte-2.91-gtk4-uninstalled) \
//       $(pkg-config --libs vte-2.91-gtk4-uninstalled) \
//       -Wl,-rpath,<vte-build>/src
//
// Run:
//   LD_LIBRARY_PATH=<vte-build>/src ./consumer_test
//
// Expected (no warnings): CONSUMER: capture1 size=N / CONSUMER: PASS

#include <gtk/gtk.h>
#include <vte/vte.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>

static GtkWidget* g_win = nullptr;
static GtkWidget* g_term = nullptr;
static int g_status = 1;
static GtkApplication* g_app = nullptr;

static void finish();
static void drain(int msec = 50) {
    gint64 end = g_get_monotonic_time() + (msec * 1000);
    while (g_get_monotonic_time() < end) {
        while (g_main_context_pending(nullptr))
            g_main_context_iteration(nullptr, FALSE);
        g_usleep(1000);
    }
}

static void on_activate(GtkApplication* app, gpointer) {
    GtkWidget* win = gtk_application_window_new(app);
    g_win = win;
    gtk_window_set_default_size(GTK_WINDOW(win), 640, 400);
    g_term = vte_terminal_new();
    vte_terminal_set_size(VTE_TERMINAL(g_term), 80, 24);
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(g_term), 10000);
    gtk_window_set_child(GTK_WINDOW(win), g_term);
    gtk_window_present(GTK_WINDOW(win));
    drain(100);

    const char* payload = "CONSUMER_PROBE_X\nsecond line\n";
    vte_terminal_feed_child(VTE_TERMINAL(g_term), payload, (glong)strlen(payload));
    drain(80);

    GBytes* snap1 = nullptr;
    vte_terminal_snapshot_capture(VTE_TERMINAL(g_term), &snap1);
    if (!snap1) {
        g_print("CONSUMER_FAIL: capture1\n");
        finish();
        return;
    }
    gsize sz1 = 0;
    g_bytes_get_data(snap1, &sz1);
    g_print("CONSUMER: capture1 size=%zu\n", sz1);

    if (!vte_terminal_snapshot_restore(VTE_TERMINAL(g_term), snap1)) {
        g_print("CONSUMER_FAIL: restore\n");
        g_bytes_unref(snap1);
        finish();
        return;
    }
    drain(80);

    GBytes* snap2 = nullptr;
    vte_terminal_snapshot_capture(VTE_TERMINAL(g_term), &snap2);
    gboolean eq = (snap2 && g_bytes_compare(snap1, snap2) == 0);
    if (snap2) g_bytes_unref(snap2);
    g_bytes_unref(snap1);

    if (!eq) {
        g_print("CONSUMER_FAIL: snap1 != snap2\n");
        finish();
        return;
    }

    g_print("CONSUMER: PASS\n");
    g_status = 0;
    finish();
}

static void finish() {
    gtk_window_destroy(GTK_WINDOW(g_win));
    g_application_quit(G_APPLICATION(g_app));
}

int main(int argc, char** argv) {
    if (!gtk_init_check()) { g_print("CONSUMER_FAIL: gtk_init\n"); return 1; }
    GtkApplication* app = gtk_application_new(
        "test.output.snapshot_consumer", G_APPLICATION_DEFAULT_FLAGS);
    g_app = app;
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return g_status;
}
