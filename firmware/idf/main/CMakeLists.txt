# The portable C library lives in ../../lib and is compiled in as plain sources.
set(LIB ${CMAKE_CURRENT_LIST_DIR}/../../lib)
idf_component_register(
    SRCS main.c emitter_rmt.c blaster.c hub.c gatt.c battery.c
         ${LIB}/lf_ir.c ${LIB}/lf_rules.c ${LIB}/lf_emitter.c ${LIB}/lf_ble.c
         ${LIB}/lf_rx.c ${LIB}/lf_hub.c ${LIB}/lf_log.c ${LIB}/lf_link.c ${LIB}/lf_battery.c ${LIB}/lf_conv.c ${LIB}/lf_haptic.c
    INCLUDE_DIRS . ${LIB}
    REQUIRES driver esp_adc esp_timer bt nvs_flash
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
