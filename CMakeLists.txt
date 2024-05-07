cmake_minimum_required(VERSION 3.28)
project("Orphee" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(sdl2 REQUIRED CONFIG)
find_package(Vulkan REQUIRED)
find_package(spdlog REQUIRED CONFIG)
find_package(VulkanMemoryAllocator REQUIRED CONFIG)
find_package(OpenEXR REQUIRED CONFIG)

add_subdirectory(imgui)

add_subdirectory(lib)

add_subdirectory(include)

add_subdirectory(sandbox)
