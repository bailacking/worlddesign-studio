include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "Build raylib examples")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries")
FetchContent_Declare(raylib
  URL https://codeload.github.com/raysan5/raylib/tar.gz/refs/tags/5.5
  URL_HASH SHA256=aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(raylib)
