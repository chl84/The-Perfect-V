cmake_minimum_required(VERSION 3.24)
project(ThePerfectVWindowsDependencies LANGUAGES C CXX)
include(FetchContent)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
foreach(feature ZLIB BZIP2 PNG HARFBUZZ BROTLI)
  set(FT_DISABLE_${feature} ON CACHE BOOL "" FORCE)
endforeach()
FetchContent_Declare(SDL
  URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
  URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
  DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/downloads" DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_Declare(freetype
  URL https://download.savannah.gnu.org/releases/freetype/freetype-2.14.1.tar.xz
  URL_HASH SHA256=32427e8c471ac095853212a37aef816c60b42052d4d9e48230bab3bdf2936ccc
  DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/downloads" DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
FetchContent_MakeAvailable(SDL freetype)
