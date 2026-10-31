add_library(kreart-brush STATIC
    src/brush_tip.cpp
    src/round_brush.cpp
    src/sensor_curve.cpp
    src/stroke_preview.cpp
    src/texture_mask.cpp
)

target_include_directories(kreart-brush PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(kreart-brush PUBLIC
    kreart-core
    PNG::PNG
)
