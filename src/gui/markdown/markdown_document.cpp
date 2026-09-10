#include "gui/markdown/markdown_document.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <regex>
#include <system_error>

namespace remin::markdown {

// ---------------------------------------------------------------------------
// PrintConfig
// ---------------------------------------------------------------------------

std::string PrintConfig::to_json() const {
    nlohmann::json j;
    j["header"] = header;
    j["footer"] = footer;
    j["show_page_numbers"] = show_page_numbers;
    j["show_toc"] = show_toc;
    j["max_toc_depth"] = max_toc_depth;
    j["font_family"] = font_family;
    j["base_font_pt"] = base_font_pt;
    return j.dump();
}

PrintConfig PrintConfig::from_json(const std::string& json) {
    PrintConfig cfg;
    if (json.empty()) return cfg;
    try {
        const nlohmann::json j = nlohmann::json::parse(json);
        if (j.contains("header")) cfg.header = j.at("header").get<std::string>();
        if (j.contains("footer")) cfg.footer = j.at("footer").get<std::string>();
        if (j.contains("show_page_numbers"))
            cfg.show_page_numbers = j.at("show_page_numbers").get<bool>();
        if (j.contains("show_toc")) cfg.show_toc = j.at("show_toc").get<bool>();
        if (j.contains("max_toc_depth"))
            cfg.max_toc_depth = j.at("max_toc_depth").get<int>();
        if (j.contains("font_family"))
            cfg.font_family = j.at("font_family").get<std::string>();
        if (j.contains("base_font_pt"))
            cfg.base_font_pt = j.at("base_font_pt").get<double>();
    } catch (const std::exception&) {
        return PrintConfig{}; // malformed -> defaults
    }
    return cfg;
}

// ---------------------------------------------------------------------------
// MarkdownDocument
// ---------------------------------------------------------------------------

std::optional<std::string> MarkdownDocument::save_pasted_image(
    const std::string& png_bytes) const {
    if (png_bytes.empty() || png_bytes.size() > 32 * 1024 * 1024)
        return std::nullopt;

    // Shared image directory: ~/remin-image/
    const char* home = std::getenv("HOME");
    if (!home) return std::nullopt;
    std::filesystem::path shared_dir = std::filesystem::path(home) / "remin-image";

    std::error_code ec;
    std::filesystem::create_directories(shared_dir, ec);
    if (ec) return std::nullopt;

    // Next free number scanning only "asset-NNN.png" names.
    int next = 1;
    for (const auto& entry : std::filesystem::directory_iterator(shared_dir, ec)) {
        if (ec) return std::nullopt;
        const std::string name = entry.path().filename().string();
        static const std::regex kAsset(R"(^asset-(\d{3})\.png$)");
        std::smatch m;
        if (std::regex_match(name, m, kAsset)) {
            const int n = std::stoi(m[1].str());
            if (n >= next) next = n + 1;
        }
    }

    char file_name[32];
    std::snprintf(file_name, sizeof(file_name), "asset-%03d.png", next);
    const std::filesystem::path target = shared_dir / file_name;
    std::ofstream out(target, std::ios::binary);
    if (!out) return std::nullopt;
    out.write(png_bytes.data(), static_cast<std::streamsize>(png_bytes.size()));
    out.close();
    if (!out) return std::nullopt;

    // Return custom URI scheme for shared images.
    return std::string("remin://images/") + file_name;
}

std::optional<std::filesystem::path> MarkdownDocument::resolve_asset(
    const std::string& ref) const {
    if (ref.empty()) return std::nullopt;

    // remin://images/asset-XXX.png -> ~/remin-image/asset-XXX.png
    if (ref.rfind("remin://images/", 0) == 0) {
        const char* home = std::getenv("HOME");
        if (!home) return std::nullopt;
        std::filesystem::path p = std::filesystem::path(home) / "remin-image" / ref.substr(15);
        std::error_code ec;
        if (std::filesystem::exists(p, ec)) return std::filesystem::canonical(p, ec);
        return std::nullopt;
    }

    // Non-local schemes (http:, https:, data:) are not files.
    const std::string scheme = "://";
    if (ref.find("://") != std::string::npos) return std::nullopt;
    if (ref.rfind("data:", 0) == 0) return std::nullopt;

    std::error_code ec;
    // assets/... -> note's asset_dir or note_dir/assets/
    if (ref.rfind("assets/", 0) == 0) {
        std::filesystem::path p;
        if (!asset_dir_.empty()) {
            p = asset_dir_ / ref.substr(7);
        } else if (!note_dir_.empty()) {
            p = note_dir_ / "assets" / ref.substr(7);
        } else {
            return std::nullopt;
        }
        if (std::filesystem::exists(p, ec)) return std::filesystem::canonical(p, ec);
        return std::nullopt;
    }
    // Absolute path
    if (ref.size() > 1 && ref[0] == '/') {
        std::filesystem::path p(ref);
        if (std::filesystem::exists(p, ec)) return std::filesystem::canonical(p, ec);
        return std::nullopt;
    }
    // Relative to note_dir
    if (!note_dir_.empty()) {
        std::filesystem::path p = note_dir_ / ref;
        if (std::filesystem::exists(p, ec)) return std::filesystem::canonical(p, ec);
    }
    // Relative to cwd
    std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (!ec) {
        std::filesystem::path p = cwd / ref;
        if (std::filesystem::exists(p, ec)) return std::filesystem::canonical(p, ec);
    }
    return std::nullopt;
}

} // namespace remin::markdown