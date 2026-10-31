add_library(kreart-core STATIC
    src/document.cpp
    src/filters.cpp
    src/selection_mask.cpp
    src/project_tracker.cpp
    src/tiled_image.cpp
)

target_include_directories(kreart-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
