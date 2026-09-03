add_library(remin_gui STATIC
  application.cpp
  session/workspace_session.cpp
  session/session_controller.cpp
  window/main_window.cpp
  window/terminal_tab_view.cpp
  window/note_tab_view.cpp
  terminal/terminal_pane.cpp
  note/note_editor.cpp
  note/markdown_preview.cpp
  theme/theme_manager.cpp
)
target_include_directories(remin_gui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_definitions(remin_gui PRIVATE
  REMIN_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources"
)
target_link_libraries(remin_gui PUBLIC
  remin_core
  remin_storage
  remin_terminal
  PkgConfig::GTKMM
  PkgConfig::VTE
  PkgConfig::ADWAITA
)

if(MD4C_FOUND)
  target_link_libraries(remin_gui PRIVATE PkgConfig::MD4C)
  target_compile_definitions(remin_gui PRIVATE REMIN_HAVE_MD4C=1)
endif()

remin_enable_warnings(remin_gui)
remin_enable_sanitizers(remin_gui)
