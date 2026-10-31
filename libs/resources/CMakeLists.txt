add_library(kreart-resources STATIC
    src/preset.cpp
    src/resource_database.cpp
)

target_include_directories(kreart-resources PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(kreart-resources PUBLIC
    kreart-brush
    LibXml2::LibXml2
    ZLIB::ZLIB
    libzip::zip
    Qt6::Core
)
