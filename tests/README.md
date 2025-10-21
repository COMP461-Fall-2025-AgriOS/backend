This test harness builds and runs the server, creates a map and a robot, invokes pathfinding and prints the resulting simulation.log.

Usage:

    python3 tests/run_tests.py

The script expects the top-level binary `agrios_backend` to be present (it will run `make build` if missing). It runs the server on port 9090 by default.
