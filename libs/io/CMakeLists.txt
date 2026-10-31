add_library(kreart-io STATIC
    src/autosave.cpp
    src/kra.cpp
    src/lzf.cpp
    src/png.cpp
    src/recovery.cpp
    src/mp4_exporter.cpp
    src/timelapse.cpp
    src/video/minih264.c
    src/wal_journal.cpp
)

target_include_directories(kreart-io PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(kreart-io PUBLIC
    kreart-core
    PNG::PNG
    libzip::zip
    LibXml2::LibXml2
    Qt6::Core
    Qt6::Gui
)
