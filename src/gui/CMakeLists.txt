add_library(remin_gui STATIC
  application.cpp
  window/main_window.cpp
  terminal/terminal_pane.cpp
)
target_include_directories(remin_gui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(remin_gui PUBLIC
  remin_core
  remin_terminal
  PkgConfig::GTKMM
  PkgConfig::VTE
  PkgConfig::ADWAITA
)

remin_enable_warnings(remin_gui)
remin_enable_sanitizers(remin_gui)
