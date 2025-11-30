Segment and export satellite farm images to a C++ map header

This directory contains a lightweight script that segments an input satellite
image into fields, roads, and buildings/obstacles using color clustering and
heuristics. It then exports a C++ header containing:

- A small occupancy grid (WIDTH, HEIGHT, GRID) where each cell has one of:
  UNKNOWN=0, FIELD=1, ROAD=2, OBSTACLE=3
- Polygons for each detected class (FIELDS, ROADS, BUILDINGS, OTHER)

Quick start

1. Install requirements (use a virtualenv):

   python3 -m venv .venv
   source .venv/bin/activate
   pip install -r scripts/requirements.txt

2. Run on an image:

    python3 scripts/segment_and_export.py path/to/map.jpg out_map.hpp --grid 200 --k 4

    The script supports two output formats controlled by `--format`:

    - `--format segmap` (default if you don't specify `--format`): produces a
       `SegMap` namespace header with `GRID`, `WIDTH`, `HEIGHT` and polygon
       vectors. Use this for the lightweight standalone header.

    - `--format map_class`: produces a header that includes `Map.h` and defines
       an inline factory function `Map createGeneratedMap()` which constructs a
       `Map` (from `internal-representations/include/Map.h`) and populates the
       grid with accessible (0) and inaccessible (1) cells. This is the format
       compatible with the project's routing code.

    Example (Map.h-compatible):

    python3 scripts/segment_and_export.py path/to/map.jpg out_map_mapclass.hpp --format map_class --grid 200 --k 4 --map-name MyFarm --map-url /maps/myfarm

      If the backend should consume the segmentation directly (recommended), pass `--out-json /path/to/out.json` to write a small JSON file the server can parse and load into the `Map` in memory.

      Example (write JSON):

      python3 scripts/segment_and_export.py path/to/map.jpg out_map_mapclass.hpp --format map_class --out-json /tmp/myfarm_map.json

3. Include `out_map.hpp` in your C++ project and read `SegMap::GRID` and the
   polygon vectors. The grid is row-major with dimensions (WIDTH, HEIGHT).

Notes and next steps

- This is a heuristic approach. For production-quality segmentation, replace
  the clustering/classification step with a pre-trained semantic segmentation
  model (e.g., DeepLabv3, U-Net) and adapt the label mapping.
- The script includes parameters for grid size, number of clusters, and
  morphological min area for filtering contours; tune them for your imagery.

Integration notes

- The `map_class` header includes `#include "Map.h"` and returns a `Map` via
   `createGeneratedMap()`. When compiling, ensure your include paths allow the
   generated header to find `Map.h` (for example, compile with `-Iinternal-representations/include`).
