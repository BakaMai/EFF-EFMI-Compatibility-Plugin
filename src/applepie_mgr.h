#pragma once
#include <stdint.h>

#define APPLEPIE_PLUGIN_API_VERSION 1

typedef struct AP_PluginInfo {
    uint32_t    apiVersion;
    const char* displayName;
    const char* description;
    const char* configFile;
    int         supportsHotDisable;
} AP_PluginInfo;
