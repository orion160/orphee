add_library(orphee_imgui)
target_sources(orphee_imgui
    PRIVATE
    imgui.cpp imgui_demo.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp
    PUBLIC
    FILE_SET imgui_root
    TYPE HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES
    imconfig.h imgui.h imgui_internal.h imstb_rectpack.h imstb_textedit.h imstb_truetype.h
)

add_subdirectory(backends)

add_subdirectory(misc)

target_link_libraries(orphee_imgui
    PUBLIC
    Vulkan::Vulkan SDL2::SDL2
)
