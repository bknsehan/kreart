cmake_minimum_required(VERSION 3.22)
project(kreart VERSION 0.2.0 LANGUAGES C CXX)
add_compile_definitions(KREART_VERSION_STRING="01.000.20261031")
add_compile_definitions(_USE_MATH_DEFINES)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(GNUInstallDirs)
include(CTest)

# Dependencies
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Svg)
if(ANDROID)
    include(FetchContent)

    # 1. libpng
    set(PNG_SHARED OFF CACHE BOOL "" FORCE)
    set(PNG_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        libpng
        GIT_REPOSITORY https://github.com/pnggroup/libpng.git
        GIT_TAG v1.6.43
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(libpng)
    if(TARGET png_static AND NOT TARGET PNG::PNG)
        target_include_directories(png_static INTERFACE
            $<BUILD_INTERFACE:${libpng_BINARY_DIR}>
            $<BUILD_INTERFACE:${libpng_SOURCE_DIR}>
        )
        add_library(PNG::PNG ALIAS png_static)
        set(PNG_FOUND TRUE)
    endif()

    # 2. libxml2
    set(LIBXML2_WITH_ICONV OFF CACHE BOOL "" FORCE)
    set(LIBXML2_WITH_LZMA OFF CACHE BOOL "" FORCE)
    set(LIBXML2_WITH_PYTHON OFF CACHE BOOL "" FORCE)
    set(LIBXML2_WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(LIBXML2_WITH_PROGRAMS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        libxml2
        GIT_REPOSITORY https://gitlab.gnome.org/GNOME/libxml2.git
        GIT_TAG v2.12.6
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(libxml2)
    if(TARGET LibXml2 AND NOT TARGET LibXml2::LibXml2)
        add_library(LibXml2::LibXml2 ALIAS LibXml2)
        set(LibXml2_FOUND TRUE)
    endif()

    # 3. libzip
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(BUILD_REGRESS OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_DOC OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        libzip
        GIT_REPOSITORY https://github.com/nih-at/libzip.git
        GIT_TAG v1.10.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(libzip)
    if(TARGET zip AND NOT TARGET libzip::zip)
        add_library(libzip::zip ALIAS zip)
    endif()

    # 4. zlib (provided by Android NDK)
    find_package(ZLIB REQUIRED)
else()
    find_package(PNG REQUIRED)
    find_package(LibXml2 REQUIRED)
    find_package(ZLIB REQUIRED)

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBZIP QUIET IMPORTED_TARGET libzip)
    endif()
    if(TARGET PkgConfig::LIBZIP)
        add_library(libzip::zip ALIAS PkgConfig::LIBZIP)
    else()
        find_package(libzip CONFIG REQUIRED)
    endif()
endif()

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()

add_subdirectory(data)
add_subdirectory(libs)
add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
