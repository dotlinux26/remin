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

# Install bundled resources (logo, styles) for runtime lookup.
function(remin_install_resources resources_dir)
  install(DIRECTORY ${resources_dir}/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/remin/resources
  )
endfunction()

# Install the app icon into the hicolor theme + a launcher .desktop entry.
function(remin_install_app_icon resources_dir)
  # Themed icons (GTK taskbar / window icon via icon_name, incl. tab markers).
  # Do NOT install index.theme — it is provided by the hicolor-icon-theme package
  # (declared in Depends); installing it causes a file conflict.
  install(DIRECTORY ${resources_dir}/icons/hicolor/scalable/apps/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    FILES_MATCHING PATTERN "*.svg"
  )
  # Launcher entry for the application menu / taskbar.
  install(FILES ${resources_dir}/remin.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
  )
endfunction()

# Install AppStream metadata for software-center integration.
function(remin_install_appstream resources_dir)
  install(FILES ${resources_dir}/remin.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
  )
endfunction()
