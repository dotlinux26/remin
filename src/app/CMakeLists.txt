# The single `remin` binary. Internal libraries link in.
add_executable(remin main.cpp)
target_include_directories(remin PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(remin PRIVATE
  remin_core
  remin_storage
  remin_terminal
  remin_ipc
  remin_cli
)

if(REMIN_BUILD_GUI)
  target_link_libraries(remin PRIVATE remin_gui)
  target_compile_definitions(remin PRIVATE REMIN_HAS_GUI=1)
endif()

remin_enable_warnings(remin)
remin_enable_sanitizers(remin)
remin_install_binary(remin)
