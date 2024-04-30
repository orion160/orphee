add_library(orphee_core)
target_link_libraries(orphee_core
    PUBLIC Vulkan::Vulkan
    PRIVATE spdlog::spdlog
)

add_subdirectory(orphee)
