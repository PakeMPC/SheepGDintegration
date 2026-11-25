cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(SheepGDintegration VERSION 1.1.0)

# Add all source files inside src (recursively)
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)

# Set up the mod binary
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

# fmt 12 (lo trae el SDK de Geode 6) crea un modulo de C++20 si ve Clang >= 16
# con Ninja, pero CMake no sabe escanear modulos con clang-cl y la generacion
# falla ("fmt-module ... does not provide a way to discover the import graph").
# El mod no usa modulos, asi que se apaga.
set(FMT_MODULE OFF CACHE BOOL "" FORCE)

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# Set up dependencies, resources, and link Geode.
setup_geode_mod(${PROJECT_NAME})

# libwebp, solo el decodificador. Las miniaturas de levelthumbs.prevter.me son
# WebP y el cocos de GD 2.2091 ya no las sabe leer. Estatica, y sin herramientas
# ni extras: solo se compila lo que enlaza el mod.
CPMAddPackage(
    NAME libwebp
    GITHUB_REPOSITORY webmproject/libwebp
    GIT_TAG v1.6.0
    EXCLUDE_FROM_ALL YES
    OPTIONS
        "BUILD_SHARED_LIBS OFF"
        "WEBP_USE_THREAD OFF"
        "WEBP_BUILD_ANIM_UTILS OFF"
        "WEBP_BUILD_CWEBP OFF"
        "WEBP_BUILD_DWEBP OFF"
        "WEBP_BUILD_GIF2WEBP OFF"
        "WEBP_BUILD_IMG2WEBP OFF"
        "WEBP_BUILD_VWEBP OFF"
        "WEBP_BUILD_WEBPINFO OFF"
        "WEBP_BUILD_LIBWEBPMUX OFF"
        "WEBP_BUILD_WEBPMUX OFF"
        "WEBP_BUILD_EXTRAS OFF"
)
target_link_libraries(${PROJECT_NAME} webpdecoder)
# La biblioteca solo exporta la raiz de su repo como ruta de cabeceras (ella misma
# incluye "src/webp/..."). Se anade src/ para poder escribir <webp/decode.h>.
target_include_directories(${PROJECT_NAME} PRIVATE ${libwebp_SOURCE_DIR}/src)
