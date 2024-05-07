target_sources(orphee_imgui
    PRIVATE
    cpp/imgui_stdlib.cpp
)

target_sources(orphee_imgui
    PUBLIC
    FILE_SET imgui_root
    FILES
    cpp/imgui_stdlib.h
)
