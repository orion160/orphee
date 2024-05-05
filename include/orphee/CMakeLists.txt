set(ORPHEE_HEADERS orphee.hpp vulkan.hpp vkManager.hpp)

target_sources(orphee_core
    PUBLIC FILE_SET orphee_core_hdrs
    TYPE HEADERS
    FILES ${ORPHEE_HEADERS}
)
