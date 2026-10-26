# The portable C library lives in ../../lib and is compiled in as plain sources.
set(LIB ${CMAKE_CURRENT_LIST_DIR}/../../lib)
idf_component_register(
    SRCS main.c emitter_rmt.c blaster.c
         ${LIB}/lf_ir.c ${LIB}/lf_rules.c ${LIB}/lf_emitter.c ${LIB}/lf_ble.c
    INCLUDE_DIRS . ${LIB}
    REQUIRES driver esp_timer
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
