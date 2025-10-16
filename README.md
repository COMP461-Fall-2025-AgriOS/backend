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


