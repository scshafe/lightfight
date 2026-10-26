#include "sdkconfig.h"

#include "blaster.h"
#include "hub.h"

void app_main(void)
{
#if CONFIG_LF_ROLE_BLASTER
    blaster_run();
#else
    hub_run();
#endif
}
