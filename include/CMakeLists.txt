target_sources(orphee_core
    PUBLIC
    FILE_SET orphee_core_hdrs
    TYPE HEADERS
    BASE_DIRS $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> $<INSTALL_INTERFACE:include>
)

add_subdirectory(orphee)
