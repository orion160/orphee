set(ORPHEE_HEADERS OrpheeDefs.hpp vkDefs.hpp vkManager.hpp App.hpp RenderStage.hpp)

target_sources(orphee_core
    PUBLIC FILE_SET orphee_core_hdrs
    TYPE HEADERS
    FILES ${ORPHEE_HEADERS}
)
