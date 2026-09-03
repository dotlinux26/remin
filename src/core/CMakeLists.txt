add_library(remin_core STATIC
  autosave.cpp
  pane/pane.cpp
  serialization.cpp
  workspace_core.cpp
  workspace_lock.cpp
)

target_include_directories(remin_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(remin_core PUBLIC remin_json)

remin_enable_warnings(remin_core)
remin_enable_sanitizers(remin_core)
