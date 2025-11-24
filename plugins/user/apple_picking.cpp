#include "plugins/PluginAPI.h"
#include <string>
#include <cstring>

static const HostAPI* g_api = nullptr;
static std::string g_moduleId;

static void plugin_callback(const char* context) {
    const char* ctx = context ? context : "";
    if (g_api && g_api->log) {
        g_api->log(g_api->host_ctx, 0, 
            (std::string("Apple picking plugin invoked with ctx=") + ctx).c_str());
    }
    
    if (g_api && g_api->log) {
        g_api->log(g_api->host_ctx, 0, "Opening gripper for apple picking");
        g_api->log(g_api->host_ctx, 0, "Extending arm to reach apple");
        g_api->log(g_api->host_ctx, 0, "Closing gripper to grasp apple");
        g_api->log(g_api->host_ctx, 0, "Twisting to detach apple from branch");
        g_api->log(g_api->host_ctx, 0, "Retracting arm with apple");
        g_api->log(g_api->host_ctx, 0, "Depositing apple into basket");
        g_api->log(g_api->host_ctx, 0, "Apple picked successfully");
    }
}

extern "C" int plugin_start(const HostAPI* api, const char* moduleId) {
    if (!api || !moduleId) return -1;
    g_api = api;
    g_moduleId = moduleId;
    if (g_api->register_callback) {
        g_api->register_callback(g_api->host_ctx, g_moduleId.c_str(), &plugin_callback);
    }
    if (g_api->log) {
        g_api->log(g_api->host_ctx, 0, 
            (std::string("Apple picking plugin started: ") + g_moduleId).c_str());
    }
    return 0;
}

extern "C" void plugin_stop() {
    if (g_api && g_api->unregister_callback) {
        g_api->unregister_callback(g_api->host_ctx, g_moduleId.c_str());
    }
    if (g_api && g_api->log) {
        g_api->log(g_api->host_ctx, 0, 
            (std::string("Apple picking plugin stopped: ") + g_moduleId).c_str());
    }
    g_moduleId.clear();
    g_api = nullptr;
}