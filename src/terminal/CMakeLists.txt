add_library(remin_terminal STATIC
  pty/pty_linux.cpp
  pty/event_loop.cpp
  shell/shell.cpp
)
target_include_directories(remin_terminal PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(remin_terminal PUBLIC remin_core)

remin_enable_warnings(remin_terminal)
remin_enable_sanitizers(remin_terminal)
