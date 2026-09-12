#pragma once

#include <string>

namespace remin::gui {

// Absolute path to the bundled resources directory (icons, styles). Resolved at
// runtime, in priority order:
//   1. $REMIN_RESOURCE_DIR (explicit override for tests/troubleshooting)
//   2. <executable_dir>/../share/remin/resources (installed layout; also the
//      layout used inside the AppImage and the .deb)
//   3. REMIN_RESOURCE_DIR baked at build time (dev builds point this at the
//      source tree; release packaging may set it to the install prefix)
std::string resource_dir();

// resource_dir() + "/" + rel.
std::string resource_path(const char* rel);

} // namespace remin::gui