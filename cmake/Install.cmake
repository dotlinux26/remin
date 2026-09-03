# Install rules / packaging.

include(GNUInstallDirs)

function(remin_install_binary target)
  install(TARGETS ${target}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endfunction()

function(remin_install_lib target)
  install(TARGETS ${target}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endfunction()
