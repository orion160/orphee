add_executable(sandbox)
target_sources(sandbox PRIVATE sandbox.cpp)
target_link_libraries(sandbox PRIVATE orphee_core SDL2::SDL2 SDL2::SDL2main)

add_custom_command(TARGET sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:sandbox> $<TARGET_FILE_DIR:sandbox>
    COMMAND_EXPAND_LISTS
)

add_executable(imgui_sandbox)
target_sources(imgui_sandbox PRIVATE imgui_sandbox.cpp)
target_link_libraries(imgui_sandbox PRIVATE orphee_core SDL2::SDL2 SDL2::SDL2main imgui)

add_custom_command(TARGET imgui_sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:imgui_sandbox> $<TARGET_FILE_DIR:imgui_sandbox>
    COMMAND_EXPAND_LISTS
)
