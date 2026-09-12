add_library(remin_gui STATIC
  application.cpp
  resources.cpp
  ui/context_menu.cpp
  session/workspace_session.cpp
  session/session_controller.cpp
  window/main_window.cpp
  window/terminal_tab_view.cpp
  window/note_tab_view.cpp
  window/directory_tree_panel.cpp
  window/settings_dialog.cpp
  window/commands_page.cpp
  terminal/terminal_pane.cpp
  terminal/pane_history_tracker.cpp
  terminal/history_file_watcher.cpp
  note/note_editor.cpp
  note/markdown_preview.cpp
  markdown/markdown_ast.cpp
  markdown/markdown_css.cpp
  markdown/markdown_document.cpp
  markdown/markdown_draw.cpp
  markdown/markdown_html.cpp
  markdown/markdown_inline_html.cpp
  markdown/markdown_layout.cpp
  markdown/markdown_pango.cpp
  markdown/markdown_pdf_export.cpp
  markdown/markdown_style.cpp
)
target_include_directories(remin_gui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)
# Compile-time fallback resource dir. Dev builds point it at the source tree;
# release packaging may override with -DREMIN_RESOURCE_DIR="" to force the
# runtime <exe_dir>/../share/remin/resources resolution (see resources.cpp).
set(REMIN_RESOURCE_DIR_DEFAULT "${CMAKE_SOURCE_DIR}/resources")
set(REMIN_RESOURCE_DIR "${REMIN_RESOURCE_DIR_DEFAULT}" CACHE STRING
    "Compile-time fallback resources directory (empty = relocatable)")
target_compile_definitions(remin_gui PRIVATE
  REMIN_RESOURCE_DIR="${REMIN_RESOURCE_DIR}"
)
target_link_libraries(remin_gui PUBLIC
  remin_core
  remin_storage
  remin_terminal
  PkgConfig::GTKMM
  PkgConfig::VTE
  PkgConfig::ADWAITA
  PkgConfig::GTKSOURCE
)

# Vendored md4c 0.5.3 (third_party/md4c). The distro libmd4c 0.4.8 rejects
# GFM table delimiter cells shorter than 3 dashes ("| - |"), so we ship the
# fixed parser directly instead of depending on a system version.
target_sources(remin_gui PRIVATE
  markdown/third_party/md4c/md4c.c
  markdown/third_party/md4c/entity.c
)
target_include_directories(remin_gui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/markdown/third_party/md4c
)
set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/markdown/third_party/md4c/md4c.c
  ${CMAKE_CURRENT_SOURCE_DIR}/markdown/third_party/md4c/entity.c
  PROPERTIES LANGUAGE C
)
target_compile_definitions(remin_gui PRIVATE REMIN_HAVE_MD4C=1)

remin_enable_warnings(remin_gui)
remin_enable_sanitizers(remin_gui)
