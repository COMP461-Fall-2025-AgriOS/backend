#pragma once

// Minimal C API for plugins to integrate with Agrios.
// Plugins must export the following symbols:
//   int plugin_start(const struct HostAPI* api, const char* moduleId);
//   void plugin_stop();
// The host will call plugin_start() with a HostAPI pointer and a moduleId (string).
// plugin_start should return 0 on success.

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*plugin_callback_fn)(const char* context);

// Host-provided functions. host_ctx is an opaque pointer passed back to the functions
// so the host can associate calls with an instance.
typedef void (*host_register_fn)(void* host_ctx, const char* moduleId, plugin_callback_fn cb);
typedef void (*host_unregister_fn)(void* host_ctx, const char* moduleId);
typedef void (*host_log_fn)(void* host_ctx, int level, const char* msg);

struct HostAPI {
    void* host_ctx;
    host_register_fn register_callback;
    host_unregister_fn unregister_callback;
    host_log_fn log;
};

#ifdef __cplusplus
}
#endif
