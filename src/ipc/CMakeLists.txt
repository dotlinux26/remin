add_library(remin_ipc STATIC
  server/ipc_server.cpp
  client/ipc_client.cpp
)
target_include_directories(remin_ipc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(remin_ipc PUBLIC remin_core)

remin_enable_warnings(remin_ipc)
remin_enable_sanitizers(remin_ipc)
