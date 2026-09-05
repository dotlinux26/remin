/*
 * Fidelity Gate Test (design §5.5) — direct parser feed round-trip
 * VTE (realized window) → feed ANSI via vte_terminal_feed → capture via get_text_range
 * Goal: textual scrollback round-trip EXACT; color/format fidelity NOT preserved.
 */
#include <gtk/gtk.h>
#include <vte/vte.h>
#include <string>
#include <iostream>
#include <vector>
#include <algorithm>
#include <unistd.h>

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

static Term make_term(GtkApplication* app, int cols = 80, int rows = 24) {
    Term t;
    t.win = gtk_application_window_new(app);
    gtk_window_set_default_size(GTK_WINDOW(t.win), cols * 10, rows * 20);
    t.term = vte_terminal_new();
    vte_terminal_set_size(VTE_TERMINAL(t.term), cols, rows);
    gtk_window_set_child(GTK_WINDOW(t.win), t.term);
    gtk_window_present(GTK_WINDOW(t.win));
    return t;
}

// Drain main loop with real time delay to allow GTK4 layout + render cycles
static void drain(int msec = 50) {
    gint64 end = g_get_monotonic_time() + (msec * 1000);
    while (g_get_monotonic_time() < end) {
        while (g_main_context_pending(nullptr)) {
            g_main_context_iteration(nullptr, FALSE);
        }
        g_usleep(1000);
    }
}

static void feed_term(GtkWidget* term, std::string_view data) {
    vte_terminal_feed(VTE_TERMINAL(term), data.data(), static_cast<gssize>(data.size()));
    drain(30);
}

static std::string capture_visible(GtkWidget* term) {
    // Capture exact 80x24 grid snapshot via get_text_range
    char* txt = vte_terminal_get_text_range(
        VTE_TERMINAL(term),
        0, 0,   // start row, col
        24, 80, // end row (exclusive), col
        nullptr, nullptr, nullptr
    );
    std::string out = txt ? txt : "";
    g_free(txt);
    return out;
}

// ANSI battery with \r\n (VTE parser requires CR+LF for proper line handling)
// Each entry: {name, input_sequence, expected_visual_token_for_semantic_check}
static std::vector<std::tuple<std::string, std::string, std::string>> kBattery = {
    {"plain", "line one\r\nline two\r\n", "line"},
    {"color", "\033[31mred text\033[0m\r\n", "red"},
    {"bold",  "\033[1mbold\033[0m\r\n", "bold"},
    {"cursor_left_overwrite", "over\033[2Drite\r\n", "ovrite"},
    {"wide",  "héllo \xe2\x86\x92 w\xc3\xb6rld\r\n", "wörld"},
    {"cr_lf", "cr-test\r\n", "cr-test"},
    {"wrap",  "a very long line that should wrap across columns in the output because it exceeds eighty characters by quite a bit\r\n", "very long"},
};

// Convert LF→CRLF in captured text to simulate PTY ONLCR behavior
static std::string lf_to_crlf(std::string_view s) {
    std::string out; out.reserve(s.size() + 16);
    for (char c : s) { if (c == '\n') { out += '\r'; out += '\n'; } else out += c; }
    return out;
}

static void on_activate(GtkApplication* app, gpointer) {
    Term t1 = make_term(app, 80, 24);
    drain(100);

    for (const auto& [name, seq, _] : kBattery) {
        feed_term(t1.term, seq);
    }

    std::string cap1 = capture_visible(t1.term);
    std::cerr << "cap1 size=" << cap1.size() << "\n";
    std::cerr << "cap1>>>\n" << cap1 << "<<<\n";

    Term t2 = make_term(app, 80, 24);
    drain(100);

    feed_term(t2.term, lf_to_crlf(cap1));

    std::string cap2 = capture_visible(t2.term);
    std::cerr << "cap2 size=" << cap2.size() << "\n";
    std::cerr << "cap2>>>\n" << cap2 << "<<<\n";

    // Gate 1: TEXTUAL ROUNDTRIP EXACT
    CHECK(cap1 == cap2, "ROUNDTRIP_EXACT: cap1 != cap2");
    if (cap1 != cap2) {
        for (size_t i = 0; i < std::min(cap1.size(), cap2.size()); i++) {
            if (cap1[i] != cap2[i]) {
                std::cerr << "first diff at " << i << " cap1=" << (int)cap1[i] << " cap2=" << (int)cap2[i] << "\n";
                break;
            }
        }
    }

    // Gate 2: SEMANTIC CONTENT
    size_t pos = 0;
    for (const auto& [name, seq, expected_token] : kBattery) {
        size_t found = cap2.find(expected_token, pos);
        CHECK(found != std::string::npos, "semantic: missing token '" + expected_token + "'");
        pos = (found == std::string::npos) ? pos : found + 1;
    }

    // Gate 3: COLOR/FORMAT NOT PRESERVED
    CHECK(cap2.find('\033') == std::string::npos, "color leak: ESC char in captured text");

    gtk_window_destroy(GTK_WINDOW(t1.win));
    gtk_window_destroy(GTK_WINDOW(t2.win));
    g_application_quit(G_APPLICATION(app));
}

int main(int argc, char** argv) {
    if (!gtk_init_check()) {
        std::cout << "SKIP: no display\n";
        return 0;
    }
    GtkApplication* app = gtk_application_new("remin.fidelity_gate", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    if (g_failures == 0) {
        std::cout << "fidelity_gate_test: OK\n";
        return 0;
    }
    std::cerr << "fidelity_gate_test: " << g_failures << " failure(s)\n";
    return 1;
}