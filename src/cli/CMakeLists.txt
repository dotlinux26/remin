add_library(remin_cli STATIC
  commands/request_dispatcher.cpp
)
target_include_directories(remin_cli PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(remin_cli PUBLIC
  remin_core
)

remin_enable_warnings(remin_cli)
remin_enable_sanitizers(remin_cli)
