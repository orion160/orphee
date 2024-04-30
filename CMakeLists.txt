cmake_minimum_required(VERSION 3.28)
project("Orphee" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(sdl2 REQUIRED CONFIG)
find_package(Vulkan REQUIRED)
find_package(spdlog REQUIRED CONFIG)

add_subdirectory(lib)
add_subdirectory(include)

add_subdirectory(sandbox)

install(TARGETS orphee_core
    FILE_SET orphee_core_hdrs
)
install(TARGETS sandbox)
