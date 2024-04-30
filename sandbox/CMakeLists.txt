add_executable(sandbox)
target_sources(sandbox PRIVATE Sandbox.cpp)
target_link_libraries(sandbox PRIVATE orphee_core SDL2::SDL2 SDL2::SDL2main)

add_custom_command(TARGET sandbox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:sandbox> $<TARGET_FILE_DIR:sandbox>
    COMMAND_EXPAND_LISTS
)
