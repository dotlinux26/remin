# Each unit test is a small standalone executable (no framework yet).
# When the suite grows, consider switching to GoogleTest.

set(REMIN_TEST_SOURCES
  unit/workspace_core_test.cpp
  unit/pane_tree_test.cpp
  unit/serialization_test.cpp
  unit/autosave_test.cpp
  unit/workspace_lock_test.cpp
  unit/persistence_policy_test.cpp
  unit/history_test.cpp
  unit/terminal_cwd_test.cpp
)

foreach(test_src ${REMIN_TEST_SOURCES})
  get_filename_component(test_name ${test_src} NAME_WE)
  add_executable(${test_name} ${test_src})
  target_include_directories(${test_name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(${test_name} PRIVATE remin_core remin_storage remin_json)
  remin_enable_warnings(${test_name})
  remin_enable_sanitizers(${test_name})
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

if(REMIN_BUILD_GUI)
  # Fidelity gate test: VTE live -> ANSI/control -> capture -> feed -> compare
  add_executable(fidelity_gate_test unit/fidelity_gate_test.cpp)
  target_include_directories(fidelity_gate_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(fidelity_gate_test PRIVATE PkgConfig::GTKMM PkgConfig::VTE)
  remin_enable_warnings(fidelity_gate_test)
  remin_enable_sanitizers(fidelity_gate_test)
  add_test(NAME fidelity_gate_test COMMAND fidelity_gate_test)
endif()
