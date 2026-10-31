add_library(kreart-render STATIC
    src/framebuffer.cpp
    src/gl_renderer.cpp
)

target_include_directories(kreart-render PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(kreart-render PUBLIC
    kreart-core
    kreart-brush
    Qt6::Gui
    Qt6::OpenGL
)
