# Agrios Backend

## Server Setup
1. Clone the repository:
   ```bash
   git clone https://github.com/COMP461-Fall-2025-AgriOS/backend.git
   ```
2. Navigate to the project directory:
   ```bash
   cd backend
   ```
3. Build the binary:
   ```bash
   make build
   ```
4. Run the server:
   ```bash
   ./agrios-backend --port <port>
   ```
   Replace `<port>` with the desired port number (default is 8080).

5. **Set up a tunnel**:
   If you need to access the server from a remote machine, set up an SSH tunnel:
   ```bash
   ssh -L <local-port>:localhost:<server-port> user@<server-ip>
   ```
   Replace:
   - `<local-port>` with the port you want to use on your local machine (e.g., 15000).
   - `<server-port>` with the port the server is running on (e.g., 8080).
   - `user` with your username on the server.
   - `<server-ip>` with the IP address of the server (find using hostname -I).

   After setting up the tunnel, you can access the server locally at `http://localhost:<local-port>`.

6. The server should now be running and accessible at `http://localhost:<local-port>`.

## To Make a Request
1. Use `curl` or any HTTP client to make requests to the server. For example:
   - To test the `GET /robots/` endpoint:
     ```bash
     curl http://localhost:<local-port>/robots/
     ```
   - To test the `GET /robots/{id}` endpoint:
     ```bash
     curl http://localhost:<local-port>/robots/{id}
     ```
     Replace `{id}` with the actual robot ID.

2. Ensure the tunnel is active if you're accessing the server remotely.

## Notes
- The server will respond with appropriate handlers for the registered endpoints.
- If you encounter issues, ensure the port is open and the tunnel is correctly configured.

## Module callbacks (runtime)

The server provides a `ModuleManager` singleton to register runtime callbacks for modules. Example usage from a planner or other runtime component:

```cpp
#include "ModuleManager.h"

// Register a callback
ModuleManager::instance().registerCallback("<module-uuid>", [](const std::string& ctx){
   // ctx can be JSON or any encoded context your module expects
   std::cout << "Module called with ctx=" << ctx << std::endl;
});

// Later, invoke a module by id
ModuleManager::instance().invoke("<module-uuid>", "{\"robotId\":\"abc\"}");

// Invoke all registered callbacks
ModuleManager::instance().invokeAll("{\"broadcast\":true}");
```

The server API also supports CRUD for modules under `/modules` which manipulates the set of available modules.


## Plugin development

Agrios supports loading external plugins (shared objects) at startup. Plugins must implement a minimal C API so they can be built independently and loaded with `dlopen`.

Plugin API (header: `plugins/PluginAPI.h`)

- `int plugin_start(const HostAPI* api, const char* moduleId)` — called when the host loads the plugin. Return 0 on success.
- `void plugin_stop()` — called when the host unloads the plugin.

HostAPI provides the following callbacks the plugin can call:

- `host_api->register_callback(host_ctx, moduleId, plugin_callback_fn cb)` — register a callback that will be invoked when `ModuleManager::invoke(moduleId, ctx)` is called.
- `host_api->unregister_callback(host_ctx, moduleId)` — unregister the callback.
- `host_api->log(host_ctx, level, msg)` — log a message through the host.

Example plugin
--------------
An example plugin is provided at `plugins/examples/watering/`. Build it with:

```sh
cd plugins/examples/watering
make
```

Start the server and point it at the example plugin directory:

```sh
./agrios_backend --port 8080 --plugins-dir ./plugins/examples/watering
```

The server will dlopen any `.so` files in the plugins directory and call their `plugin_start` function. The example registers a callback under the module id `libwatering_example` (filename without `.so`). You can trigger it by invoking the module from code via `ModuleManager::instance().invoke("libwatering_example", "{...}")`.

Notes and best practices
------------------------
- Plugins should be compiled with a compatible C++ runtime and ABI for the host (same compiler and standard library versions when possible).
- For a forward-compatible API, consider versioning `PluginAPI.h` and verifying compatibility in `plugin_start`.
- Keep plugin initialization fast and avoid blocking long-running work on the host thread; spawn worker threads if needed.

Plugin endpoints (for front-end)
--------------------------------
Agrios exposes simple HTTP endpoints so a front-end can discover available plugins, select which ones to enable, and invoke them.

- List available plugins (files in the plugins directory):

   GET /plugins

   Response: JSON array of module ids (filenames without `.so`).

   Example:

   ```sh
   curl http://localhost:8080/plugins
   # => ["libwatering_example","another_plugin"]
   ```

- Get currently enabled plugins:

   GET /enabled-plugins

   Response: JSON array of enabled module ids.

   Example:

   ```sh
   curl http://localhost:8080/enabled-plugins
   # => ["libwatering_example"]
   ```

- Set enabled plugins (front-end posts JSON array of module ids):

   POST /enabled-plugins

   Request body: JSON array of strings, e.g. `["libwatering_example"]`.

   Example:

   ```sh
   curl -X POST http://localhost:8080/enabled-plugins -d '["libwatering_example"]'
   ```

- Invoke an enabled plugin by id (pass context in the body):

   POST /invoke/{moduleId}

   Example:

   ```sh
   curl -X POST http://localhost:8080/invoke/libwatering_example -d '{"robotId":"robot-1"}'
   ```

Notes
-----
- The plugin listing endpoint reads the configured plugins directory (the directory passed to the server using `--plugins-dir` or the default `./plugins`). Make sure the server is started with the correct path.
- The `POST /enabled-plugins` endpoint uses a simple parser and accepts a JSON-like array of quoted strings. If you need stricter JSON handling, I can add a small JSON library to parse requests robustly.



