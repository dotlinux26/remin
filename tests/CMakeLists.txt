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
  unit/golden_history_test.cpp
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

  # VTE snapshot round-trip test: uses vte_terminal_snapshot_capture/restore
  # from the minimal VTE 0.76 patch (remin-vte-extension Phase 0).
  add_executable(vte_snapshot_roundtrip_test unit/vte_snapshot_roundtrip_test.cpp)
  target_include_directories(vte_snapshot_roundtrip_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(vte_snapshot_roundtrip_test PRIVATE PkgConfig::GTKMM PkgConfig::VTE)
  remin_enable_warnings(vte_snapshot_roundtrip_test)
  remin_enable_sanitizers(vte_snapshot_roundtrip_test)
  add_test(NAME vte_snapshot_roundtrip_test COMMAND vte_snapshot_roundtrip_test)

  # VTE Phase-0E Critical Validation: comprehensive behavioral equivalence tests
  add_executable(vte_critical_validation_test unit/vte_critical_validation_test.cpp)
  target_include_directories(vte_critical_validation_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(vte_critical_validation_test PRIVATE PkgConfig::GTKMM PkgConfig::VTE)
  remin_enable_warnings(vte_critical_validation_test)
  remin_enable_sanitizers(vte_critical_validation_test)
  add_test(NAME vte_critical_validation_test COMMAND vte_critical_validation_test)

  # VTE P0-G external-consumer test: snapshot API consumed from outside the VTE
  # source tree (public API only). Requires a patched VTE build at runtime.
  add_executable(vte_snapshot_consumer_test unit/vte_snapshot_consumer_test.cpp)
  target_include_directories(vte_snapshot_consumer_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(vte_snapshot_consumer_test PRIVATE PkgConfig::GTKMM PkgConfig::VTE)
  remin_enable_warnings(vte_snapshot_consumer_test)
  remin_enable_sanitizers(vte_snapshot_consumer_test)
  add_test(NAME vte_snapshot_consumer_test COMMAND vte_snapshot_consumer_test)

  # VTE P0-H1/H2 DB integration test: binary snapshot stored as a SQLite BLOB
  # through the real SqliteStorage and restored into a fresh VTE — the automated
  # analogue of the real-restart acceptance at the storage boundary.
  add_executable(vte_snapshot_db_roundtrip_test unit/vte_snapshot_db_roundtrip_test.cpp)
  target_include_directories(vte_snapshot_db_roundtrip_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(vte_snapshot_db_roundtrip_test PRIVATE PkgConfig::GTKMM PkgConfig::VTE remin_storage remin_core)
  remin_enable_warnings(vte_snapshot_db_roundtrip_test)
  remin_enable_sanitizers(vte_snapshot_db_roundtrip_test)
  add_test(NAME vte_snapshot_db_roundtrip_test COMMAND vte_snapshot_db_roundtrip_test)
endif()
