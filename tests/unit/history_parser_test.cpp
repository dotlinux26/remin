#include "core/terminal/history_parser.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace remin::core::terminal;

static int g_failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "FAIL: " << #cond << " at " << __LINE__ << "\n";      \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        if ((a) != (b)) {                                                      \
            std::cerr << "FAIL: " << #a " == " #b << " (" << (a) << " != " << (b) << ") at " << __LINE__ << "\n"; \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

int main() {
    // Test 1: Plain format - simple commands
    {
        std::string content = "ls\npwd\necho hello\n";
        auto recs = parse_history_buffer(content);
        CHECK_EQ(recs.size(), 3);
        CHECK_EQ(recs[0].command, "ls");
        CHECK_EQ(recs[1].command, "pwd");
        CHECK_EQ(recs[2].command, "echo hello");
        CHECK_EQ(recs[0].timestamp_us, 0);
    }

    // Test 2: Plain format - ignores empty lines
    {
        std::string content = "ls\n\npwd\n\necho test\n";
        auto recs = parse_history_buffer(content);
        CHECK_EQ(recs.size(), 3);
        CHECK_EQ(recs[0].command, "ls");
        CHECK_EQ(recs[1].command, "pwd");
        CHECK_EQ(recs[2].command, "echo test");
    }

    // Test 3: Timestamped format - single line commands
    {
        std::string content = "#1700000000\nls\n#1700000001\npwd\n#1700000002\necho hello\n";
        auto recs = parse_history_buffer(content);
        CHECK_EQ(recs.size(), 3);
        CHECK_EQ(recs[0].command, "ls");
        CHECK_EQ(recs[0].timestamp_us, 1700000000LL * 1'000'000);
        CHECK_EQ(recs[1].command, "pwd");
        CHECK_EQ(recs[1].timestamp_us, 1700000001LL * 1'000'000);
        CHECK_EQ(recs[2].command, "echo hello");
        CHECK_EQ(recs[2].timestamp_us, 1700000002LL * 1'000'000);
    }

    // Test 4: Timestamped format - multiline commands
    {
        std::string content = "#1700000000\nls -la\n#1700000001\nprintf 'line1\\nline2'\n#1700000002\npwd\n";
        auto recs = parse_history_buffer(content);
        CHECK_EQ(recs.size(), 3);
        CHECK_EQ(recs[0].command, "ls -la");
        CHECK_EQ(recs[1].command, "printf 'line1\\nline2'");
        CHECK_EQ(recs[2].command, "pwd");
    }

    // Test 5: Timestamped format - command with literal newlines (multiline entry)
    {
        // In real bash history with HISTTIMEFORMAT, a multiline command looks like:
        // #timestamp
        // line1
        // line2
        // #next_timestamp
        std::string content = "#1700000000\nprintf 'hello'\nworld\n#1700000001\necho done\n";
        auto recs = parse_history_buffer(content);
        // The parser treats lines between timestamps as part of the same command
        CHECK_EQ(recs.size(), 2);
        CHECK_EQ(recs[0].command, "printf 'hello'\nworld");
        CHECK_EQ(recs[1].command, "echo done");
    }

    // Test 6: Commands with quotes and special characters
    {
        std::string content = "echo \"hello world\"\necho 'single quotes'\ngit commit -m \"fix: don't break\"\n";
        auto recs = parse_history_buffer(content);
        CHECK_EQ(recs.size(), 3);
        CHECK_EQ(recs[0].command, "echo \"hello world\"");
        CHECK_EQ(recs[1].command, "echo 'single quotes'");
        CHECK_EQ(recs[2].command, "git commit -m \"fix: don't break\"");
    }

    // Test 7: Empty buffer
    {
        std::string content = "";
        auto recs = parse_history_buffer(content);
        CHECK_EQ(recs.size(), 0);
    }

    // Test 8: Only whitespace
    {
        std::string content = "   \n\t\n  \n";
        auto recs = parse_history_buffer(content);
        CHECK_EQ(recs.size(), 0);
    }

    // Test 9: Mixed timestamp detection - first non-empty determines format
    {
        std::string content = "\n\n#1700000000\nls\npwd\n";
        auto recs = parse_history_buffer(content);
        // First non-empty is #1700000000, so timestamped format
        // But "pwd" has no timestamp - it becomes part of previous command
        CHECK_EQ(recs.size(), 1);
        CHECK_EQ(recs[0].command, "ls\npwd");
    }

    // Test 10: Duplicate commands preserved (parser doesn't dedupe)
    {
        std::string content = "ls\nls\npwd\nls\n";
        auto recs = parse_history_buffer(content);
        CHECK_EQ(recs.size(), 4);
        CHECK_EQ(recs[0].command, "ls");
        CHECK_EQ(recs[1].command, "ls");
        CHECK_EQ(recs[2].command, "pwd");
        CHECK_EQ(recs[3].command, "ls");
    }

    // Test 11: Commands containing newlines in plain format - each line is separate entry
    {
        // In plain format without timestamps, each line is a separate command
        std::string content = "printf 'a\\nb'\necho done\n";
        auto recs = parse_history_buffer(content);
        CHECK_EQ(recs.size(), 2);
        CHECK_EQ(recs[0].command, "printf 'a\\nb'");
        CHECK_EQ(recs[1].command, "echo done");
    }

    // Test 12: File with only timestamp markers
    {
        std::string content = "#1700000000\n#1700000001\n#1700000002\n";
        auto recs = parse_history_buffer(content);
        CHECK_EQ(recs.size(), 0); // no commands, just timestamps
    }

    // Test 13: Leading/trailing whitespace on commands
    {
        std::string content = "  ls  \n\tpwd\t\n  echo hello  \n";
        auto recs = parse_history_buffer(content);
        CHECK_EQ(recs.size(), 3);
        CHECK_EQ(recs[0].command, "ls");
        CHECK_EQ(recs[1].command, "pwd");
        CHECK_EQ(recs[2].command, "echo hello");
    }

    // Test 14: Very long command
    {
        std::string long_cmd = "git commit -m \"";
        long_cmd += std::string(1000, 'x');
        long_cmd += "\"";
        std::string content = long_cmd + "\n";
        auto recs = parse_history_buffer(content);
        CHECK_EQ(recs.size(), 1);
        CHECK_EQ(recs[0].command, long_cmd);
    }

    if (g_failures == 0) {
        std::cout << "history_parser_test: OK\n";
        return 0;
    }
    std::cerr << "history_parser_test: " << g_failures << " failure(s)\n";
    return 1;
}