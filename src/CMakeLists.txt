file(GLOB_RECURSE app_sources
    *.c
    *.cpp
)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    REQUIRES bt nvs_flash
)