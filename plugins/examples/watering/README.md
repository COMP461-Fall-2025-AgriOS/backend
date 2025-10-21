# Watering Plugin Example

This is a minimal example plugin for Agrios. It registers a callback under the module id derived from the filename (without `.so`).

Build
-----
From this directory:

```sh
make
```

This produces `libwatering_example.so`. Copy it into the host `plugins/` directory (project root `plugins/`) or point the server at this directory via `--plugins-dir`.

Run
---
1. Build the host (project root):

```sh
make build
```

2. Start the server (default port 8080) and point at the directory containing the plugin:

```sh
./agrios_backend --port 8080 --plugins-dir ./plugins/examples/watering
```

3. From another process, trigger the plugin via the ModuleManager (example):

If you have a small program or can call into the running process, call:

```cpp
ModuleManager::instance().invoke("libwatering_example", "{\"robotId\":\"robot-1\"}");
```

Or expose an HTTP endpoint that invokes `ModuleManager::instance().invoke(...)`.

Notes
-----
- This example uses the `HostAPI` to register and unregister callbacks. The module id used by the host loader is the filename without the `.so` suffix.
- The plugin uses `HostAPI::log` to log messages back to the host.
