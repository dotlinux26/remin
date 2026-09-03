# Vendored SQLite amalgamation (C).
add_library(remin_sqlite STATIC
  ${REMIN_SQLITE_DIR}/sqlite3.c
)
target_include_directories(remin_sqlite PUBLIC ${REMIN_SQLITE_DIR})
target_compile_definitions(remin_sqlite PRIVATE
  SQLITE_THREADSAFE=1
  SQLITE_DEFAULT_FOREIGN_KEYS=1
)
set_target_properties(remin_sqlite PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

add_library(remin_storage STATIC
  sqlite/sqlite_db.cpp
  storage.cpp
)
target_include_directories(remin_storage PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(remin_storage PUBLIC
  remin_core
  remin_sqlite
)

remin_enable_warnings(remin_storage)
remin_enable_sanitizers(remin_storage)
