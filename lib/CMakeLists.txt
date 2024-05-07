add_library(orphee_vma)
target_link_libraries(orphee_vma
    PUBLIC
    Vulkan::Vulkan GPUOpen::VulkanMemoryAllocator
)

add_library(orphee_core)
target_link_libraries(orphee_core
    PUBLIC
    orphee_vma Vulkan::Vulkan
    PRIVATE
    spdlog::spdlog
)
target_compile_definitions(orphee_core
    PUBLIC
    VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=0 VMA_VULKAN_VERSION=1003000
)

add_subdirectory(orphee)
