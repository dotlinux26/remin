# Third-party and system dependency discovery.
#
# System GUI stack comes from distro (GTK4/gtkmm/VTE/libadwaita/GLib/Pango).
# Embedded libs (SQLite, nlohmann) are vendored under third_party/.

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann/nlohmann/json.hpp")
  set(REMIN_NLOHMANN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann")
else()
  message(FATAL_ERROR "nlohmann/json not found; expected third_party/nlohmann/nlohmann/json.hpp")
endif()

# Vendored SQLite amalgamation (C library)
set(REMIN_SQLITE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite")
if(NOT EXISTS "${REMIN_SQLITE_DIR}/sqlite3.c" OR NOT EXISTS "${REMIN_SQLITE_DIR}/sqlite3.h")
  message(FATAL_ERROR "SQLite amalgamation not found; expected third_party/sqlite/sqlite3.[ch]")
endif()

find_package(PkgConfig REQUIRED)

set(REMIN_PKG_DEPS "")

if(REMIN_BUILD_GUI)
  pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-4.0)
  pkg_check_modules(VTE   REQUIRED IMPORTED_TARGET vte-2.91-gtk4)
  pkg_check_modules(ADWAITA REQUIRED IMPORTED_TARGET libadwaita-1)
  pkg_check_modules(GTKSOURCE REQUIRED IMPORTED_TARGET gtksourceview-5)
  # Optional: CommonMark parser for the note-editor markdown preview.
  # Guards graphical dependency so core/storage always stay dependency-free.
  pkg_check_modules(MD4C  IMPORTED_TARGET md4c)
endif()

# nlohmann/json is header-only, expose include dir globally for core/storage/json use.
add_library(remin_json INTERFACE)
target_include_directories(remin_json INTERFACE "${REMIN_NLOHMANN_DIR}")
target_compile_definitions(remin_json INTERFACE JSON_USE_IMPLICIT_CONVERSIONS=0)
