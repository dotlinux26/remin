# Compiler warnings: enabled aggressively for core/lib targets.

function(remin_enable_warnings target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wshadow
      -Wconversion
      -Wsign-conversion
      -Wformat=2
      -Wnull-dereference
      -Wstrict-overflow=5
      -Wundef
      # nlohmann/json triggers strict-overflow; we keep other warnings.
      -Wno-strict-overflow
    )
  endif()
endfunction()
