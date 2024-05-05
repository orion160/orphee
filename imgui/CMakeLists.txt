add_library(imgui)
target_sources(imgui
    PRIVATE imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp
    PUBLIC FILE_SET imgui_root
    TYPE HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
)

add_subdirectory(backends)
add_subdirectory(misc)

target_link_libraries(imgui PUBLIC Vulkan::Vulkan SDL2::SDL2)
