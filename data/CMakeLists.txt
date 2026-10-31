include(GNUInstallDirs)

install(FILES io.github.kreart.Kreart.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(FILES icons/io.github.kreart.Kreart.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)
