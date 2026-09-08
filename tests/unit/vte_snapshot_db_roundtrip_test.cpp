/*
 * VTE Snapshot → SQLite BLOB Round-trip Test (P0-H1/H2)
 *
 * Proves the Remin persistence pipeline for binary terminal snapshots end-to-end
 * through the REAL SqliteStorage (not an in-memory fake):
 *
 *   VTE A (realized) --feed content--> snapshot_capture -> vector<uint8_t>
 *     -> SqliteStorage::store_snapshot()      [BLOB column terminal_snapshots]
 *     -> SqliteStorage::load_snapshot()       [BLOB read back]
 *   VTE B (realized) --snapshot_restore(vector)<-- 
 *
 * Gate 1: the BLOB survives the SQLite store/load boundary byte-for-byte
 *         (store_hdr == load_hdr).
 * Gate 2: after restore into a fresh VTE B, re-capture gives byte-identical
 *         snapshot to A's original capture (full state rebuilt).
 *
 * This is the automated analogue of the "real restart" acceptance (Remin A ->
 * checkpoint BLOB -> Remin B -> restore), exercised at the storage boundary
 * that capture/checkpoint/restore actually use.
 */
#include <gtk/gtk.h>
#include <vte/vte.h>

#include "storage/storage.hpp"

#include <string>
#include <vector>
#include <iostream>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

static int g_failures = 0;
#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << msg << " at " << __LINE__ << "\n"; \
            ++g_failures; \
        } \
    } while (0)

using namespace std::literals;

struct Term {
    GtkWidget* win = nullptr;
    GtkWidget* term = nullptr;
};

static Term make_term(GtkApplication* app, int cols = 80, int rows = 24, glong scrollback = 10000) {
    Term t;
    t.win = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(t.win), cols * 10, rows * 20);
    t.term = vte_terminal_new();
    vte_terminal_set_size(VTE_TERMINAL(t.term), cols, rows);
    vte_terminal_set_scrollback_lines(VTE_TERMINAL(t.term), scrollback);
    gtk_window_set_child(GTK_WINDOW(t.win), t.term);
    gtk_window_present(GTK_WINDOW(t.win));
    return t;
}

static void drain(int msec = 50) {
    gint64 end = g_get_monotonic_time() + (msec * 1000);
    while (g_get_monotonic_time() < end) {
        while (g_main_context_pending(nullptr))
            g_main_context_iteration(nullptr, FALSE);
        g_usleep(1000);
    }
}

static void feed_term(GtkWidget* term, std::string_view data) {
    vte_terminal_feed(VTE_TERMINAL(term), data.data(), static_cast<gssize>(data.size()));
    drain(30);
}

static std::vector<std::uint8_t> snapshot_vec(GtkWidget* term, bool& ok) {
    GBytes* snap = nullptr;
    vte_terminal_snapshot_capture(VTE_TERMINAL(term), &snap);
    ok = snap != nullptr;
    std::vector<std::uint8_t> out;
    if (!snap) return out;
    gsize size;
    const guint8* data = static_cast<const guint8*>(g_bytes_get_data(snap, &size));
    out.assign(data, data + size);
    g_bytes_unref(snap);
    return out;
}

static void on_activate(GtkApplication* app, gpointer) {
    Term t1 = make_term(app);
    drain(100);

    // Populate terminal A with representative content (scrollback + visible).
    const std::vector<std::string> lines = {
        "echo REMIN_DB_MARKER_A",
        "ls -la /tmp",
        "printf 'session persisted marker\\n'",
        "whoami",
    };
    for (const auto& l : lines) {
        feed_term(t1.term, l + "\r");
        feed_term(t1.term, "\r\n");
    }
    feed_term(t1.term, "echo REMIN_DB_MARKER_B\r\n");
    drain(80);

    bool ok_a = false;
    auto snap_a = snapshot_vec(t1.term, ok_a);
    CHECK(ok_a && !snap_a.empty(), "capture(A) failed or empty");

    // --- Real SqliteStorage BLOB round-trip --------------------------------
    const char* tmpdir = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
    std::string db_path = std::string(tmpdir) + "/remin_snapshot_db_test_" +
                          std::to_string((long)getpid()) + ".db";
    std::remove(db_path.c_str());

    remin::storage::SqliteStorage store(db_path);
    CHECK(store.ok(), "SqliteStorage failed to init");
    if (store.ok()) {
        remin::core::PaneId pid{"pane-snapshot-test"};
        store.store_snapshot(pid, snap_a);
        auto snap_loaded = store.load_snapshot(pid);

        // Gate 1: byte-for-byte across the SQLite BLOB boundary.
        CHECK(snap_loaded.size() == snap_a.size(), "BLOB size changed across DB store/load");
        CHECK(snap_loaded == snap_a, "Gate1: BLOB mutated across SQLite store/load");
        std::cerr << "DB BLOB store/load: size=" << snap_loaded.size()
                  << " bytes, byte-identical=" << (snap_loaded == snap_a) << "\n";

        // Restore from the DB-loaded bytes into a fresh VTE B.
        Term t2 = make_term(app);
        drain(100);
        GBytes* bytes = g_bytes_new(snap_loaded.data(), snap_loaded.size());
        gboolean restored = vte_terminal_snapshot_restore(VTE_TERMINAL(t2.term), bytes);
        g_bytes_unref(bytes);
        CHECK(restored, "restore(DB-loaded snapshot) into B failed");
        drain(80);

        // Gate 2: re-capture of B reproduces A byte-for-byte.
        bool ok_b = false;
        auto snap_b = snapshot_vec(t2.term, ok_b);
        CHECK(ok_b, "capture(B) failed");
        CHECK(snap_b == snap_a, "Gate2: snapshot(B) != snapshot(A) after DB round-trip");

        gtk_window_destroy(GTK_WINDOW(t2.win));
    }

    gtk_window_destroy(GTK_WINDOW(t1.win));
    std::remove(db_path.c_str());
    g_application_quit(G_APPLICATION(app));
}

int main(int argc, char** argv) {
    if (!gtk_init_check()) {
        std::cout << "SKIP: no display\n";
        return 0;
    }
    GtkApplication* app = gtk_application_new("remin.vte_snapshot_db_roundtrip", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    if (g_failures == 0) {
        std::cout << "vte_snapshot_db_roundtrip_test: OK\n";
        return 0;
    }
    std::cerr << "vte_snapshot_db_roundtrip_test: " << g_failures << " failure(s)\n";
    return 1;
}
