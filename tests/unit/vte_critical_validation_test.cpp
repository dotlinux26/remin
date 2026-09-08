/*
 * VTE Phase-0E Critical Validation Test Suite
 * 
 * Comprehensive behavioral equivalence testing for VTE snapshot/restore.
 * Tests structural AND behavioral equivalence per the P0-E mandate.
 */
#include <gtk/gtk.h>
#include <vte/vte.h>
#include <string>
#include <iostream>
#include <vector>
#include <map>
#include <sstream>
#include <unistd.h>
#include <cstdint>
#include <cstring>

static int g_failures = 0;

#define TEST_BEGIN(name) \
    do { \
        std::cerr << "\n========== " << name << " ==========" << std::endl; \
    } while(0)

#define TEST_END() \
    do { \
        std::cerr << "========== END ==========" << std::endl; \
    } while(0)

#define ASSERT_EQ(a, b, msg) \
    do { \
        if (!((a) == (b))) { \
            std::cerr << "  FAIL: " << msg << " (" << __LINE__ << "): " #a " != " #b \
                      << " (got " << (a) << ", expected " << (b) << ")" << std::endl; \
            g_failures++; \
        } \
    } while(0)

#define ASSERT_NE(a, b, msg) \
    do { \
        if ((a) == (b)) { \
            std::cerr << "  FAIL: " << msg << " (" << __LINE__ << "): " #a " == " #b \
                      << " (both " << (a) << ")" << std::endl; \
            g_failures++; \
        } \
    } while(0)

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "  FAIL: " << msg << " (" << __LINE__ << "): " #cond " is false" << std::endl; \
            g_failures++; \
        } \
    } while(0)

#define ASSERT_FALSE(cond, msg) \
    do { \
        if (cond) { \
            std::cerr << "  FAIL: " << msg << " (" << __LINE__ << "): " #cond " is true" << std::endl; \
            g_failures++; \
        } \
    } while(0)

#define LOG(msg) std::cerr << "  " << msg << std::endl

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

static std::string snapshot_hex(GtkWidget* term, bool& ok) {
    GBytes* snap = nullptr;
    vte_terminal_snapshot_capture(VTE_TERMINAL(term), &snap);
    ok = snap != nullptr;
    if (!ok)
        return "";
    gsize size;
    const guint8* data = static_cast<const guint8*>(g_bytes_get_data(snap, &size));
    std::string out(reinterpret_cast<const char*>(data), size);
    g_bytes_unref(snap);
    return out;
}

static void compare_snapshots(GtkWidget* a, GtkWidget* b, const char* context) {
    bool ok_a = false, ok_b = false;
    std::string snap_a = snapshot_hex(a, ok_a);
    std::string snap_b = snapshot_hex(b, ok_b);
    ASSERT_TRUE(ok_a && ok_b, "snapshot capture for comparison");
    if (snap_a != snap_b) {
        std::cerr << "  FAIL [" << context << "]: snapshot byte-for-byte differs" << std::endl;
        std::cerr << "  A size=" << snap_a.size() << " B size=" << snap_b.size() << std::endl;
        g_failures++;
    }
}

// =====================================================================
// TEST 1: Ring Freeze/Thaw
// =====================================================================
static void test_ring_freeze_thaw(GtkApplication* app) {
    TEST_BEGIN("test_ring_freeze_thaw");
    
    Term a = make_term(app);
    feed_term(a.term, "echo line1\n");
    feed_term(a.term, "echo line2\n");
    feed_term(a.term, "echo line3\n");
    drain(100);
    
    bool ok = false;
    std::string snap_a = snapshot_hex(a.term, ok);
    ASSERT_TRUE(ok, "snapshot capture A");
    ASSERT_NE(snap_a.size(), 0, "snapshot non-empty");
    
    Term b = make_term(app);
    GBytes* snap_bytes = g_bytes_new(snap_a.data(), snap_a.size());
    bool restored = vte_terminal_snapshot_restore(VTE_TERMINAL(b.term), snap_bytes);
    g_bytes_unref(snap_bytes);
    ASSERT_TRUE(restored, "snapshot restore B");
    drain(100);
    
    feed_term(a.term, "echo line4\n");
    feed_term(b.term, "echo line4\n");
    drain(100);
    
    compare_snapshots(a.term, b.term, "after ring freeze/thaw + same input");
    
    TEST_END();
}

// =====================================================================
// TEST 2: Alternate Screen (smcup/rmcup)
// =====================================================================
static void test_alternate_screen(GtkApplication* app) {
    TEST_BEGIN("test_alternate_screen");
    
    Term a = make_term(app);
    feed_term(a.term, "echo main screen\n");
    feed_term(a.term, "\033[?1049h");  // smcup - enter alt screen
    feed_term(a.term, "echo alt screen\n");
    feed_term(a.term, "\033[?1049l");  // rmcup - exit alt screen
    feed_term(a.term, "echo back to main\n");
    drain(100);
    
    bool ok = false;
    std::string snap_a = snapshot_hex(a.term, ok);
    ASSERT_TRUE(ok, "snapshot capture A");
    
    Term b = make_term(app);
    GBytes* snap_bytes = g_bytes_new(snap_a.data(), snap_a.size());
    bool restored = vte_terminal_snapshot_restore(VTE_TERMINAL(b.term), snap_bytes);
    g_bytes_unref(snap_bytes);
    ASSERT_TRUE(restored, "snapshot restore B");
    drain(100);
    
    feed_term(a.term, "\033[?1049h");
    feed_term(a.term, "echo alt again\n");
    feed_term(a.term, "\033[?1049l");
    
    feed_term(b.term, "\033[?1049h");
    feed_term(b.term, "echo alt again\n");
    feed_term(b.term, "\033[?1049l");
    drain(100);
    
    compare_snapshots(a.term, b.term, "after alt screen roundtrip + same input");
    
    TEST_END();
}

// =====================================================================
// TEST 3: Cursor Wrap & DECAWM
// =====================================================================
static void test_cursor_wrap(GtkApplication* app) {
    TEST_BEGIN("test_cursor_wrap");
    
    Term a = make_term(app, 10, 5);
    // Fill lines to force wrapping
    feed_term(a.term, "12345678901234567890\n");
    feed_term(a.term, "12345678901234567890\n");
    feed_term(a.term, "12345678901234567890\n");
    feed_term(a.term, "12345678901234567890\n");
    feed_term(a.term, "12345678901234567890\n");
    drain(100);
    
    bool ok = false;
    std::string snap_a = snapshot_hex(a.term, ok);
    ASSERT_TRUE(ok, "snapshot capture A");
    
    Term b = make_term(app, 10, 5);
    GBytes* snap_bytes = g_bytes_new(snap_a.data(), snap_a.size());
    bool restored = vte_terminal_snapshot_restore(VTE_TERMINAL(b.term), snap_bytes);
    g_bytes_unref(snap_bytes);
    ASSERT_TRUE(restored, "snapshot restore B");
    drain(100);
    
    feed_term(a.term, "WRAP\n");
    feed_term(b.term, "WRAP\n");
    drain(100);
    
    compare_snapshots(a.term, b.term, "after cursor wrap + same input");
    
    TEST_END();
}

// =====================================================================
// TEST 4: Scrolling Region (DECSTBM)
// =====================================================================
static void test_scrolling_region(GtkApplication* app) {
    TEST_BEGIN("test_scrolling_region");
    
    Term a = make_term(app, 80, 24);
    feed_term(a.term, "\033[5;15r");  // Set scrolling region lines 5-15
    for (int i = 0; i < 15; i++) {
        feed_term(a.term, "scroll line\n");
    }
    feed_term(a.term, "\033[r");  // Reset scrolling region
    drain(100);
    
    bool ok = false;
    std::string snap_a = snapshot_hex(a.term, ok);
    ASSERT_TRUE(ok, "snapshot capture A");
    
    Term b = make_term(app, 80, 24);
    GBytes* snap_bytes = g_bytes_new(snap_a.data(), snap_a.size());
    bool restored = vte_terminal_snapshot_restore(VTE_TERMINAL(b.term), snap_bytes);
    g_bytes_unref(snap_bytes);
    ASSERT_TRUE(restored, "snapshot restore B");
    drain(100);
    
    feed_term(a.term, "after region\n");
    feed_term(b.term, "after region\n");
    drain(100);
    
    compare_snapshots(a.term, b.term, "after scrolling region + same input");
    
    TEST_END();
}

// =====================================================================
// TEST 5: SGR Palette & True Color
// =====================================================================
static void test_palette_truecolor(GtkApplication* app) {
    TEST_BEGIN("test_palette_truecolor");
    
    Term a = make_term(app);
    feed_term(a.term, "\033[38;2;255;128;64mTRUECOLOR\033[0m\n");
    feed_term(a.term, "\033[48;5;208mORANGE BG\033[0m\n");
    feed_term(a.term, "\033[31;42mRED ON GREEN\033[0m\n");
    drain(100);
    
    bool ok = false;
    std::string snap_a = snapshot_hex(a.term, ok);
    ASSERT_TRUE(ok, "snapshot capture A");
    
    Term b = make_term(app);
    GBytes* snap_bytes = g_bytes_new(snap_a.data(), snap_a.size());
    bool restored = vte_terminal_snapshot_restore(VTE_TERMINAL(b.term), snap_bytes);
    g_bytes_unref(snap_bytes);
    ASSERT_TRUE(restored, "snapshot restore B");
    drain(100);
    
    feed_term(a.term, "after colors\n");
    feed_term(b.term, "after colors\n");
    drain(100);
    
    compare_snapshots(a.term, b.term, "after palette/truecolor + same input");
    
    TEST_END();
}

// =====================================================================
// TEST 6: Hyperlink (OSC 8)
// =====================================================================
static void test_hyperlink_osc8(GtkApplication* app) {
    TEST_BEGIN("test_hyperlink_osc8");
    
    Term a = make_term(app);
    feed_term(a.term, "\033]8;;https://example.com\033\\link\033]8;;\033\\\n");
    drain(100);
    
    bool ok = false;
    std::string snap_a = snapshot_hex(a.term, ok);
    ASSERT_TRUE(ok, "snapshot capture A");
    
    Term b = make_term(app);
    GBytes* snap_bytes = g_bytes_new(snap_a.data(), snap_a.size());
    bool restored = vte_terminal_snapshot_restore(VTE_TERMINAL(b.term), snap_bytes);
    g_bytes_unref(snap_bytes);
    ASSERT_TRUE(restored, "snapshot restore B");
    drain(100);
    
    feed_term(a.term, "after link\n");
    feed_term(b.term, "after link\n");
    drain(100);
    
    compare_snapshots(a.term, b.term, "after OSC8 hyperlink + same input");
    
    TEST_END();
}

// =====================================================================
// TEST 7: Tab Stops (HTS, TBC)
// =====================================================================
static void test_tabstops(GtkApplication* app) {
    TEST_BEGIN("test_tabstops");
    
    Term a = make_term(app);
    feed_term(a.term, "col1\tcol2\tcol3\n");
    feed_term(a.term, "\033H");  // HTS - set tab stop at current column
    feed_term(a.term, "tab\there\n");
    feed_term(a.term, "\033[0g");  // TBC - clear tab stop at current column
    feed_term(a.term, "tab\there\n");
    drain(100);
    
    bool ok = false;
    std::string snap_a = snapshot_hex(a.term, ok);
    ASSERT_TRUE(ok, "snapshot capture A");
    
    Term b = make_term(app);
    GBytes* snap_bytes = g_bytes_new(snap_a.data(), snap_a.size());
    bool restored = vte_terminal_snapshot_restore(VTE_TERMINAL(b.term), snap_bytes);
    g_bytes_unref(snap_bytes);
    ASSERT_TRUE(restored, "snapshot restore B");
    drain(100);
    
    feed_term(a.term, "after tabs\n");
    feed_term(b.term, "after tabs\n");
    drain(100);
    
    compare_snapshots(a.term, b.term, "after tab stops + same input");
    
    TEST_END();
}

// =====================================================================
// TEST 8: Charset Designation (SCS)
// =====================================================================
static void test_charset_designation(GtkApplication* app) {
    TEST_BEGIN("test_charset_designation");
    
    Term a = make_term(app);
    feed_term(a.term, "\033(B");  // ASCII
    feed_term(a.term, "ASCII\n");
    feed_term(a.term, "\033(0");  // DEC Special Graphics
    feed_term(a.term, "jklmnopq\n");  // line drawing chars
    feed_term(a.term, "\033(B");  // back to ASCII
    feed_term(a.term, "back to ASCII\n");
    drain(100);
    
    bool ok = false;
    std::string snap_a = snapshot_hex(a.term, ok);
    ASSERT_TRUE(ok, "snapshot capture A");
    
    Term b = make_term(app);
    GBytes* snap_bytes = g_bytes_new(snap_a.data(), snap_a.size());
    bool restored = vte_terminal_snapshot_restore(VTE_TERMINAL(b.term), snap_bytes);
    g_bytes_unref(snap_bytes);
    ASSERT_TRUE(restored, "snapshot restore B");
    drain(100);
    
    feed_term(a.term, "\033(0");
    feed_term(a.term, "jklmnopq\n");
    feed_term(a.term, "\033(B");
    
    feed_term(b.term, "\033(0");
    feed_term(b.term, "jklmnopq\n");
    feed_term(b.term, "\033(B");
    drain(100);
    
    compare_snapshots(a.term, b.term, "after charset designation + same input");
    
    TEST_END();
}

// =====================================================================
// TEST 9: Resize Handling
// =====================================================================
static void test_resize_handling(GtkApplication* app) {
    TEST_BEGIN("test_resize_handling");
    
    Term a = make_term(app, 80, 24);
    feed_term(a.term, "original size\n");
    drain(50);
    vte_terminal_set_size(VTE_TERMINAL(a.term), 120, 40);
    drain(100);
    feed_term(a.term, "resized\n");
    drain(100);
    
    bool ok = false;
    std::string snap_a = snapshot_hex(a.term, ok);
    ASSERT_TRUE(ok, "snapshot capture A");
    
    Term b = make_term(app, 80, 24);
    GBytes* snap_bytes = g_bytes_new(snap_a.data(), snap_a.size());
    bool restored = vte_terminal_snapshot_restore(VTE_TERMINAL(b.term), snap_bytes);
    g_bytes_unref(snap_bytes);
    ASSERT_TRUE(restored, "snapshot restore B");
    drain(100);
    
    vte_terminal_set_size(VTE_TERMINAL(b.term), 120, 40);
    drain(100);
    feed_term(a.term, "after resize both\n");
    feed_term(b.term, "after resize both\n");
    drain(100);
    
    compare_snapshots(a.term, b.term, "after resize + same input");
    
    TEST_END();
}

// =====================================================================
// TEST 10: Mode Persistence (DECCKM, DECNM, etc.)
// =====================================================================
static void test_mode_persistence(GtkApplication* app) {
    TEST_BEGIN("test_mode_persistence");
    
    Term a = make_term(app);
    feed_term(a.term, "\033[?1h");   // DECCKM - cursor keys application mode
    feed_term(a.term, "\033[?12h");  // DECNM - local echo off
    feed_term(a.term, "\033[?25l");  // DECTCEM - hide cursor
    feed_term(a.term, "modes set\n");
    drain(100);
    
    bool ok = false;
    std::string snap_a = snapshot_hex(a.term, ok);
    ASSERT_TRUE(ok, "snapshot capture A");
    
    Term b = make_term(app);
    GBytes* snap_bytes = g_bytes_new(snap_a.data(), snap_a.size());
    bool restored = vte_terminal_snapshot_restore(VTE_TERMINAL(b.term), snap_bytes);
    g_bytes_unref(snap_bytes);
    ASSERT_TRUE(restored, "snapshot restore B");
    drain(100);
    
    feed_term(a.term, "\033[?25h");  // show cursor
    feed_term(a.term, "after modes\n");
    feed_term(b.term, "\033[?25h");
    feed_term(b.term, "after modes\n");
    drain(100);
    
    compare_snapshots(a.term, b.term, "after mode persistence + same input");
    
    TEST_END();
}

// =====================================================================
// TEST 11: Parser Boundary (split escape sequence)
// =====================================================================
static void test_parser_boundary(GtkApplication* app) {
    TEST_BEGIN("test_parser_boundary");
    
    Term a = make_term(app);
    // Feed escape sequence split across multiple feed calls
    feed_term(a.term, "\033[");  // CSI start
    drain(10);
    feed_term(a.term, "31m");    // SGR 31 (red)
    drain(10);
    feed_term(a.term, "RED");
    drain(10);
    feed_term(a.term, "\033[0m");  // reset
    drain(10);
    feed_term(a.term, "NORMAL\n");
    drain(100);
    
    bool ok = false;
    std::string snap_a = snapshot_hex(a.term, ok);
    ASSERT_TRUE(ok, "snapshot capture A");
    
    Term b = make_term(app);
    GBytes* snap_bytes = g_bytes_new(snap_a.data(), snap_a.size());
    bool restored = vte_terminal_snapshot_restore(VTE_TERMINAL(b.term), snap_bytes);
    g_bytes_unref(snap_bytes);
    ASSERT_TRUE(restored, "snapshot restore B");
    drain(100);
    
    feed_term(a.term, "after split\n");
    feed_term(b.term, "after split\n");
    drain(100);
    
    compare_snapshots(a.term, b.term, "after parser boundary + same input");
    
    TEST_END();
}

// =====================================================================
// TEST 12: Session Metadata (title, cwd, etc.)
// =====================================================================
static void test_session_metadata(GtkApplication* app) {
    TEST_BEGIN("test_session_metadata");
    
    Term a = make_term(app);
    feed_term(a.term, "\033]0;Test Window Title\007");  // OSC 0 - window title
    feed_term(a.term, "\033]7;file:///home/user/path\007");  // OSC 7 - cwd
    drain(100);
    
    bool ok = false;
    std::string snap_a = snapshot_hex(a.term, ok);
    ASSERT_TRUE(ok, "snapshot capture A");
    
    Term b = make_term(app);
    GBytes* snap_bytes = g_bytes_new(snap_a.data(), snap_a.size());
    bool restored = vte_terminal_snapshot_restore(VTE_TERMINAL(b.term), snap_bytes);
    g_bytes_unref(snap_bytes);
    ASSERT_TRUE(restored, "snapshot restore B");
    drain(100);
    
    feed_term(a.term, "after metadata\n");
    feed_term(b.term, "after metadata\n");
    drain(100);
    
    compare_snapshots(a.term, b.term, "after session metadata + same input");
    
    TEST_END();
}

// =====================================================================
// TEST 13: Ring Integrity After Restore
// =====================================================================
static void test_ring_integrity(GtkApplication* app) {
    TEST_BEGIN("test_ring_integrity");
    
    Term a = make_term(app, 80, 24, 1000);
    for (int i = 0; i < 50; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "scrollback line %03d\n", i);
        feed_term(a.term, buf);
    }
    drain(100);
    
    bool ok = false;
    std::string snap_a = snapshot_hex(a.term, ok);
    ASSERT_TRUE(ok, "snapshot capture A");
    
    Term b = make_term(app, 80, 24, 1000);
    GBytes* snap_bytes = g_bytes_new(snap_a.data(), snap_a.size());
    bool restored = vte_terminal_snapshot_restore(VTE_TERMINAL(b.term), snap_bytes);
    g_bytes_unref(snap_bytes);
    ASSERT_TRUE(restored, "snapshot restore B");
    drain(100);
    
    // Ring content equivalence: byte-for-byte snapshot comparison after restore
    bool ok_pre = false;
    std::string snap_b_pre = snapshot_hex(b.term, ok_pre);
    ASSERT_TRUE(ok_pre, "snapshot capture B after restore");
    if (snap_a != snap_b_pre) {
        std::cerr << "  FAIL: ring snapshot after restore differs"
                  << " A size=" << snap_a.size() << " B size=" << snap_b_pre.size() << std::endl;
        g_failures++;
    }
    ASSERT_EQ(snap_a, snap_b_pre, "ring content identical after restore");
    
    feed_term(a.term, "new line after restore\n");
    feed_term(b.term, "new line after restore\n");
    drain(100);
    
    compare_snapshots(a.term, b.term, "after ring integrity check + same input");
    
    TEST_END();
}

// =====================================================================
// TEST 14: Negative Control - Different Input Diverges
// =====================================================================
static void test_negative_control(GtkApplication* app) {
    TEST_BEGIN("test_negative_control");
    
    Term a = make_term(app);
    feed_term(a.term, "common base\n");
    drain(50);
    
    bool ok = false;
    std::string snap_a = snapshot_hex(a.term, ok);
    ASSERT_TRUE(ok, "snapshot capture A");
    
    Term b = make_term(app);
    GBytes* snap_bytes = g_bytes_new(snap_a.data(), snap_a.size());
    bool restored = vte_terminal_snapshot_restore(VTE_TERMINAL(b.term), snap_bytes);
    g_bytes_unref(snap_bytes);
    ASSERT_TRUE(restored, "snapshot restore B");
    drain(50);
    
    // Different input should produce DIFFERENT state
    feed_term(a.term, "input A\n");
    feed_term(b.term, "input B\n");
    drain(100);
    
    // Different input should produce DIFFERENT state (snapshot bytes must diverge)
    bool ok_a2 = false, ok_b2 = false;
    std::string snap_a2 = snapshot_hex(a.term, ok_a2);
    std::string snap_b2 = snapshot_hex(b.term, ok_b2);
    ASSERT_TRUE(ok_a2 && ok_b2, "snapshot capture after different input");
    if (snap_a2 == snap_b2) {
        std::cerr << "  FAIL: different input produced IDENTICAL snapshots" << std::endl;
        g_failures++;
    }
    
    TEST_END();
}

// =====================================================================
// Main
// =====================================================================
// Forward declaration for signal handler
static void run_tests(GApplication* app, gpointer);

int main(int argc, char** argv) {
    setenv("GTK_DEBUG", "interactive", 1);
    
    GtkApplication* app = gtk_application_new("org.remin.vte-validation", G_APPLICATION_DEFAULT_FLAGS);
    
    g_signal_connect(app, "activate", G_CALLBACK(run_tests), nullptr);
    
    g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    
    return (g_failures == 0) ? 0 : 1;
}

static void run_tests(GApplication* app, gpointer) {
    test_ring_freeze_thaw(GTK_APPLICATION(app));
    test_alternate_screen(GTK_APPLICATION(app));
    test_cursor_wrap(GTK_APPLICATION(app));
    test_scrolling_region(GTK_APPLICATION(app));
    test_palette_truecolor(GTK_APPLICATION(app));
    test_hyperlink_osc8(GTK_APPLICATION(app));
    test_tabstops(GTK_APPLICATION(app));
    test_charset_designation(GTK_APPLICATION(app));
    test_resize_handling(GTK_APPLICATION(app));
    test_mode_persistence(GTK_APPLICATION(app));
    test_parser_boundary(GTK_APPLICATION(app));
    test_session_metadata(GTK_APPLICATION(app));
    test_ring_integrity(GTK_APPLICATION(app));
    test_negative_control(GTK_APPLICATION(app));
    
    std::cerr << "\n=====================================" << std::endl;
    if (g_failures == 0) {
        std::cerr << "ALL TESTS PASSED" << std::endl;
        std::cerr << "IMPLEMENTATION_GATE: READY_FOR_IMPLEMENTATION" << std::endl;
    } else {
        std::cerr << "FAILURES: " << g_failures << std::endl;
        std::cerr << "IMPLEMENTATION_GATE: BLOCKED" << std::endl;
    }
    std::cerr << "=====================================" << std::endl;
    
    g_application_quit(app);
}