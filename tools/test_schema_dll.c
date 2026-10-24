#include <stddef.h>

#include "../include/efz_config_api.h"

static const EfzConfigSetting kSettings[] = {
    { "General", "enabled", "Enabled", "Turns the mod on.\nSecond line.", EFZ_SETTING_SWITCH, 0, 0, NULL, "1" },
    { "General", "speedPercent", NULL, NULL, EFZ_SETTING_NUMBER, 10, 400, NULL, "100" },
    { "Look", "filter", NULL, "Scaling.", EFZ_SETTING_CHOICE, 0, 0, "Linear|Point|Sharp", "Linear" },
    { NULL, "broken", NULL, NULL, EFZ_SETTING_TEXT, 0, 0, NULL, NULL },
};

EFZ_CONFIG_EXPORT const EfzConfigSchema EFZ_ConfigSchema = {
    EFZ_CONFIG_SCHEMA_VERSION, sizeof(EfzConfigSetting), "TestMod.ini",
    sizeof(kSettings) / sizeof(kSettings[0]), kSettings,
};
