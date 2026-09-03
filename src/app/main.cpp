#include "core/workspace_core.hpp"
#include "storage/storage.hpp"
#include "cli/commands/request_dispatcher.hpp"
#ifdef REMIN_HAS_GUI
#include "gui/application.hpp"
#endif

#include <nlohmann/json.hpp>
#include <cstdlib>
#include <string>
#include <vector>
#include <iostream>
#include <memory>
#include <sys/stat.h>
#include <sys/types.h>

using namespace remin;

namespace {

const char* kUsage =
    "Remin — Remember your work.\n"
    "\n"
    "Usage: remin <command> [args...]\n"
    "\n"
    "Commands:\n"
    "  gui                            Launch the GUI workspace app\n"
    "  workspace list                 Show the open workspace\n"
    "  workspace create <name>        Create and open a workspace\n"
    "  workspace open <id>            Open a workspace\n"
    "  workspace close                Close the current workspace\n"
    "  window add [title]             Add a window\n"
    "  window rename <id> <name>      Rename a window\n"
    "  snapshot create                Create a snapshot of the current workspace\n"
    "  help                           Show this help\n";

std::string data_dir() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    std::string base = (xdg && *xdg) ? xdg : (home ? std::string(home) + "/.local/share" : "/tmp");
    return base + "/remin";
}

std::unique_ptr<storage::SqliteStorage> g_storage;
std::unique_ptr<core::WorkspaceCore> g_core;

void mkdir_p(const std::string& path) {
    std::string cur;
    for (auto it = path.begin(); it != path.end(); ++it) {
        cur += *it;
        if (*it == '/' || it + 1 == path.end()) {
            ::mkdir(cur.c_str(), 0755);
        }
    }
}

void ensure_core() {
    if (g_core) return;
    auto dir = data_dir();
    mkdir_p(dir);
    g_storage = std::make_unique<storage::SqliteStorage>(dir + "/remin.db");
    if (!g_storage->ok()) {
        std::cerr << "remin: storage error: " << g_storage->error() << "\n";
        std::exit(1);
    }
    g_core = std::make_unique<core::WorkspaceCore>(g_storage.get());
}

nlohmann::json make_request(const std::string& method, nlohmann::json params = {}) {
    return {{"method", method}, {"params", params}};
}

int run_gui() {
#ifdef REMIN_HAS_GUI
    ensure_core();
    auto* app = new remin::gui::Application();
    return app->run(0, nullptr);
#else
    (void)ensure_core;
    std::cerr << "remin: built without GUI support\n";
    return 1;
#endif
}

int run_command(const std::vector<std::string>& args) {
    if (args.empty() || args[0] == "help") {
        std::cout << kUsage;
        return 0;
    }
    const std::string& cmd = args[0];

    if (cmd == "gui") return run_gui();

    ensure_core();
    cli::RequestDispatcher dispatcher(g_core.get());

    nlohmann::json req;
    nlohmann::json params = nlohmann::json::object();

    if (cmd == "workspace" && args.size() >= 2 && args[1] == "list") {
        req = make_request("workspace.list");
    } else if (cmd == "workspace" && args.size() >= 3 && args[1] == "create") {
        params["name"] = args[2];
        req = make_request("workspace.create", params);
    } else if (cmd == "workspace" && args.size() >= 3 && args[1] == "open") {
        params["workspace_id"] = args[2];
        req = make_request("workspace.open", params);
    } else if (cmd == "workspace" && args.size() >= 2 && args[1] == "close") {
        req = make_request("workspace.close");
    } else if (cmd == "window" && args.size() >= 3 && args[1] == "add") {
        params["title"] = args[2];
        req = make_request("window.add", params);
    } else if (cmd == "window" && args.size() >= 4 && args[1] == "rename") {
        params["workspace_id"] = "";
        params["window_id"] = args[2];
        params["name"] = args[3];
        req = make_request("window.rename", params);
    } else if (cmd == "window" && args.size() >= 2 && args[1] == "add") {
        params["title"] = "window";
        req = make_request("window.add", params);
    } else if (cmd == "snapshot" && args.size() >= 2 && args[1] == "create") {
        req = make_request("snapshot.create");
    } else {
        std::cerr << "remin: unknown command '" << args[0] << "'\n";
        std::cout << kUsage;
        return 1;
    }

    std::cout << dispatcher.handle(req.dump()) << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return run_command(args);
}
