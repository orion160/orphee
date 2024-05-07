target_sources(orphee_imgui
    PRIVATE
    imgui_impl_sdl2.cpp imgui_impl_vulkan.cpp
)

target_sources(orphee_imgui
    PUBLIC
    FILE_SET imgui_root
    FILES
    imgui_impl_sdl2.h imgui_impl_vulkan.h
)
