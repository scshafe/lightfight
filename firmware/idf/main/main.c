#include "esp_log.h"

#include "blaster.h"

void app_main(void)
{
#if CONFIG_LF_ROLE_BLASTER
    blaster_run();
#else
    ESP_LOGE("lightfight", "the hub role is not built yet (milestone M6)");
#endif
}
