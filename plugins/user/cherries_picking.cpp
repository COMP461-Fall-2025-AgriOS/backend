#include "plugins/PluginAPI.h"
#include <string>
#include <cstring>

static const HostAPI* g_api = nullptr;
static std::string g_moduleId;

enum CherryPickingState {
    STATE_IDLE = 0,
    STATE_SCANNING = 1,
    STATE_APPROACHING = 2,
    STATE_PICKING = 3,
    STATE_DEPOSITING = 4,
    STATE_ERROR = 5
};

static CherryPickingState g_currentState = STATE_IDLE;
static int g_cherriesPickedCount = 0;

static void log_info(const char* message) {
    if (g_api && g_api->log) {
        g_api->log(g_api->host_ctx, 0, (std::string("[CherryPicker] ") + message).c_str());
    }
}

static void log_error(const char* message) {
    if (g_api && g_api->log) {
        g_api->log(g_api->host_ctx, 2, (std::string("[CherryPicker] ERROR: ") + message).c_str());
    }
}

static void log_debug(const char* message) {
    if (g_api && g_api->log) {
        g_api->log(g_api->host_ctx, 3, (std::string("[CherryPicker] ") + message).c_str());
    }
}

static void scan_for_cherries() {
    log_debug("Scanning for ripe cherries using vision system...");
    g_currentState = STATE_SCANNING;
}

static void approach_cherry() {
    log_debug("Approaching detected cherry...");
    g_currentState = STATE_APPROACHING;
}

static void pick_cherry() {
    log_debug("Activating gripper to pick cherry...");
    g_currentState = STATE_PICKING;
    g_cherriesPickedCount++;
    std::string msg = "Cherry picked successfully. Total count: " + std::to_string(g_cherriesPickedCount);
    log_info(msg.c_str());
}

static void deposit_cherry() {
    log_debug("Depositing cherry in collection basket...");
    g_currentState = STATE_DEPOSITING;
}

static void plugin_callback(const char* context) {
    const char* ctx = context ? context : "";
    
    log_debug((std::string("Callback invoked with context: ") + ctx).c_str());
    
    if (strlen(ctx) == 0) {
        log_info("Starting cherry picking sequence...");
        g_currentState = STATE_IDLE;
        return;
    }
    
    if (strcmp(ctx, "start") == 0) {
        log_info("Initiating cherry picking operation...");
        g_currentState = STATE_IDLE;
        g_cherriesPickedCount = 0;
        scan_for_cherries();
    }
    else if (strcmp(ctx, "scan") == 0) {
        scan_for_cherries();
    }
    else if (strcmp(ctx, "approach") == 0) {
        approach_cherry();
    }
    else if (strcmp(ctx, "pick") == 0) {
        pick_cherry();
    }
    else if (strcmp(ctx, "deposit") == 0) {
        deposit_cherry();
        g_currentState = STATE_IDLE;
    }
    else if (strcmp(ctx, "stop") == 0) {
        log_info("Stopping cherry picking operation...");
        std::string final_msg = "Cherry picking completed. Total cherries picked: " + std::to_string(g_cherriesPickedCount);
        log_info(final_msg.c_str());
        g_currentState = STATE_IDLE;
    }
    else if (strcmp(ctx, "status") == 0) {
        std::string status = "Current state: " + std::to_string(g_currentState) + 
                           ", Cherries picked: " + std::to_string(g_cherriesPickedCount);
        log_info(status.c_str());
    }
    else if (strcmp(ctx, "auto") == 0) {
        log_info("Starting automated cherry picking cycle...");
        scan_for_cherries();
        approach_cherry();
        pick_cherry();
        deposit_cherry();
        g_currentState = STATE_IDLE;
    }
    else {
        log_error((std::string("Unknown context command: ") + ctx).c_str());
        g_currentState = STATE_ERROR;
    }
}

extern "C" int plugin_start(const HostAPI* api, const char* moduleId) {
    if (!api || !moduleId) return -1;
    
    g_api = api;
    g_moduleId = moduleId;
    g_currentState = STATE_IDLE;
    g_cherriesPickedCount = 0;
    
    if (g_api->register_callback) {
        g_api->register_callback(g_api->host_ctx, g_moduleId.c_str(), &plugin_callback);
    }
    
    if (g_api->log) {
        g_