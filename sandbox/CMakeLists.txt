add_executable(sandbox)
target_sources(sandbox
    PRIVATE
    sandbox.cpp
)
target_link_libraries(sandbox
    PRIVATE
    orphee_core SDL2::SDL2 SDL2::SDL2main
)

add_custom_command(TARGET sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:sandbox> $<TARGET_FILE_DIR:sandbox>
    COMMAND_EXPAND_LISTS
)

add_executable(imgui_sandbox)
target_sources(imgui_sandbox
    PRIVATE
    imgui_sandbox.cpp
)
target_link_libraries(imgui_sandbox
    PRIVATE
    orphee_core orphee_imgui SDL2::SDL2 SDL2::SDL2main
)

add_custom_command(TARGET imgui_sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:imgui_sandbox> $<TARGET_FILE_DIR:imgui_sandbox>
    COMMAND_EXPAND_LISTS
)


add_executable(compute_sandbox)
target_sources(compute_sandbox
    PRIVATE
    compute_sandbox.cpp
)
target_link_libraries(compute_sandbox
    PRIVATE
    orphee_core OpenEXR::OpenEXR
)
add_custom_command(TARGET compute_sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:compute_sandbox> $<TARGET_FILE_DIR:compute_sandbox>
    COMMAND_EXPAND_LISTS
)
