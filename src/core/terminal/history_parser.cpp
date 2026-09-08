#include "core/terminal/history_parser.hpp"

#include <fstream>
#include <sstream>
#include <system_error>
#include <cctype>

namespace remin::core::terminal {

namespace {

// Trim whitespace from both ends
static inline std::string trim(const std::string& s) {
    auto start = s.begin();
    while (start != s.end() && std::isspace(static_cast<unsigned char>(*start))) ++start;
    auto end = s.end();
    while (end != start && std::isspace(static_cast<unsigned char>(*(end - 1)))) --end;
    return std::string(start, end);
}

// Check if a line looks like a Bash timestamp marker: #<digits>
static inline bool is_timestamp_line(const std::string& line) {
    if (line.empty() || line[0] != '#') return false;
    for (std::size_t i = 1; i < line.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(line[i]))) return false;
    }
    return line.size() > 1;
}

} // namespace

std::vector<CommandRecord> parse_history_buffer(const std::string& content) {
    std::vector<CommandRecord> result;

    if (content.empty()) return result;

    std::istringstream iss(content);
    std::string line;

    // First pass: detect format by checking if first non-empty line is a timestamp
    std::string first_nonempty;
    std::streampos pos = iss.tellg();
    while (std::getline(iss, first_nonempty)) {
        if (!trim(first_nonempty).empty()) break;
    }
    iss.seekg(pos);

    const bool has_timestamps = is_timestamp_line(trim(first_nonempty));

    if (has_timestamps) {
        // Timestamped format: #timestamp\ncommand (command may span multiple lines)
        std::int64_t current_ts = 0;
        std::string current_cmd;

        while (std::getline(iss, line)) {
            std::string trimmed = trim(line);

            if (is_timestamp_line(trimmed)) {
                // Emit previous command if any
                if (!current_cmd.empty()) {
                    CommandRecord rec;
                    rec.command = current_cmd;
                    rec.timestamp_us = current_ts * 1'000'000; // seconds to microseconds
                    result.push_back(std::move(rec));
                }
                // Parse new timestamp
                current_ts = std::stoll(trimmed.substr(1));
                current_cmd.clear();
            } else if (!trimmed.empty() || !current_cmd.empty()) {
                // Part of current command (including empty lines within a multiline command)
                if (!current_cmd.empty()) current_cmd += '\n';
                current_cmd += line; // keep original line (not trimmed) for multiline fidelity
            }
        }
        // Emit last command
        if (!current_cmd.empty()) {
            CommandRecord rec;
            rec.command = current_cmd;
            rec.timestamp_us = current_ts * 1'000'000;
            result.push_back(std::move(rec));
        }
    } else {
        // Plain format: one command per line (ignore empty lines)
        while (std::getline(iss, line)) {
            std::string cmd = trim(line);
            if (!cmd.empty()) {
                CommandRecord rec;
                rec.command = std::move(cmd);
                rec.timestamp_us = 0;
                result.push_back(std::move(rec));
            }
        }
    }

    return result;
}

std::vector<CommandRecord> parse_history_file(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) return {};

    // Read entire file into string
    std::string content;
    ifs.seekg(0, std::ios::end);
    content.reserve(static_cast<std::size_t>(ifs.tellg()));
    ifs.seekg(0, std::ios::beg);
    content.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());

    return parse_history_buffer(content);
}

} // namespace remin::core::terminal