#!/usr/bin/env python3
"""
Segment a satellite farm image into fields, roads, and buildings/obstacles,
then export a C++ header that contains: polygons per class and a compact
occupancy/grid with cell labels.

Usage:
  python3 scripts/segment_and_export.py input.jpg out_map.hpp --grid 200 --k 4

Outputs:
  - A C++ header (`out_map.hpp`) which defines a map namespace with:
    width, height, enum CellType, std::vector<uint8_t> grid (row-major),
    and vector of polygons (each polygon is a vector of pairs).

Notes:
  - This uses a lightweight KMeans color-clustering + heuristics approach.
  - Parameters can be tuned; for better accuracy use a learned segmentation model.
"""

import argparse
import cv2
import numpy as np
from sklearn.cluster import KMeans
import textwrap
import os


def parse_args():
    p = argparse.ArgumentParser(description="Segment farmland image and export C++ map header")
    p.add_argument("input_image", help="Path to input satellite image")
    p.add_argument("output_header", help="Path to output C++ header (.hpp)")
    p.add_argument("--grid", type=int, default=200, help="Grid width (cells) for occupancy grid; height will preserve aspect")
    p.add_argument("--k", type=int, default=4, help="Number of color clusters for KMeans")
    p.add_argument("--resize", type=int, default=800, help="Resize max dimension for segmentation speed (pixels)")
    p.add_argument("--min_area", type=int, default=100, help="Minimum contour area to keep (pixels)")
    p.add_argument("--road-pad", type=int, default=1, help="Amount of padding (dilation iterations) to apply to road cells in the occupancy grid")
    p.add_argument("--format", choices=["segmap", "map_class"], default="map_class",
                   help="Output header format: 'segmap' produces the SegMap namespace; 'map_class' produces a Map factory compatible with Map.h (default)")
    p.add_argument("--map-name", help="Optional name to embed in the generated Map (defaults to image basename)")
    p.add_argument("--map-url", default="", help="Optional mapUrl string to embed in the generated Map")
    p.add_argument("--out-json", default="", help="Optional path to write a JSON representation: {width,height,grid}")
    return p.parse_args()


def load_and_resize(path, max_dim):
    img = cv2.imread(path, cv2.IMREAD_COLOR)
    if img is None:
        raise FileNotFoundError(f"Cannot read image: {path}")
    h, w = img.shape[:2]
    scale = 1.0
    if max(h, w) > max_dim:
        scale = float(max_dim) / float(max(h, w))
        img = cv2.resize(img, (int(w * scale), int(h * scale)), interpolation=cv2.INTER_AREA)
    return img, scale


def cluster_colors(img, k=4, sample=10000, random_state=42):
    h, w = img.shape[:2]
    pixels = img.reshape(-1, 3).astype(np.float32) / 255.0
    if pixels.shape[0] > sample:
        idx = np.random.choice(pixels.shape[0], sample, replace=False)
        train = pixels[idx]
    else:
        train = pixels
    kmeans = KMeans(n_clusters=k, random_state=random_state)
    kmeans.fit(train)
    centers = kmeans.cluster_centers_
    # assign all pixels
    labels = kmeans.predict(pixels)
    labels = labels.reshape(h, w)
    return labels, centers

def classify_clusters(centers):
    mapping = {}
    for i, c in enumerate(centers):
        r, g, b = c[2], c[1], c[0]
        
        brightness = (r + g + b) / 3.0
        maxc = max(r, g, b)
        minc = min(r, g, b)
        saturation = 0 if maxc == 0 else (maxc - minc) / maxc
        
        # Earth tone detection for brown fields
        # Brown typically has R > G > B and moderate values
        is_earth_tone = (r > g and g > b and 
                        brightness > 0.2 and brightness < 0.7 and
                        saturation > 0.1 and saturation < 0.6)
        
        # Vegetation index (simplified NDVI-like)
        vegetation_index = (g - r) / (g + r + 1e-8) if (g + r) > 0 else -1
        
        is_green_vegetation = (vegetation_index > 0.1 and saturation > 0.2)
        is_brown_vegetation = (is_earth_tone and abs(r - g) < 0.3)  # brown fields
        
        if is_green_vegetation or is_brown_vegetation:
            cls = 'FIELD'
        elif saturation < 0.2 and brightness > 0.4 and brightness < 0.8:
            cls = 'ROAD'
        elif brightness > 0.7 and saturation < 0.45:
            cls = 'BUILDING'
        else:
            cls = 'OTHER'
        mapping[i] = cls
    return mapping

def mask_for_label(labels, idx):
    return (labels == idx).astype(np.uint8) * 255


def postprocess_mask(mask):
    # clean small holes / speckles
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5,5))
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel, iterations=1)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel, iterations=2)
    return mask


def polygons_from_mask(mask, min_area=100):
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    polys = []
    for cnt in contours:
        area = cv2.contourArea(cnt)
        if area < min_area:
            continue
        # simplify contour
        epsilon = 0.01 * cv2.arcLength(cnt, True)
        approx = cv2.approxPolyDP(cnt, epsilon, True)
        poly = [(int(p[0][0]), int(p[0][1])) for p in approx]
        if len(poly) >= 3:
            polys.append(poly)
    return polys


def build_occupancy_grid(labels, class_map, grid_width):
    # Map classes to small integer codes
    # 0 = UNKNOWN, 1 = FIELD, 2 = ROAD, 3 = OBSTACLE
    h, w = labels.shape
    aspect = h / float(w)
    grid_h = int(round(grid_width * aspect))
    # create cell assign by nearest neighbor sampling
    grid = np.zeros((grid_h, grid_width), dtype=np.uint8)
    # create mapping from class name to code
    class_to_code = {'FIELD':1, 'ROAD':2, 'BUILDING':3, 'OTHER':3}
    for gy in range(grid_h):
        for gx in range(grid_width):
            # map to image coords
            ix = int(gx * w / grid_width)
            iy = int(gy * h / grid_h)
            lbl = labels[iy, ix]
            clsname = class_map.get(lbl, 'OTHER')
            grid[gy, gx] = class_to_code.get(clsname, 0)
    return grid


def cpp_escape(s: str) -> str:
    return s.replace('"', '\\"')


def write_cpp_header(out_path, image_name, scale, polys_by_class, grid):
    # Default: produce a SegMap-style header with GRID and polygons. This function
    # is kept for backward compatibility. Use write_map_class_header(...) to
    # produce a Map.h-compatible factory function.
    h, w = grid.shape
    basename = os.path.basename(out_path)
    guard = ("_" + os.path.splitext(basename)[0] + "_HPP_").upper()
    with open(out_path, 'w') as f:
        f.write(f"// Generated by segment_and_export.py from {cpp_escape(image_name)}\n")
        f.write(f"// scale={scale}\n\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n#include <cstdint>\n#include <vector>\n#include <utility>\n\nnamespace SegMap {{\n\n    constexpr int WIDTH = {w};\n    constexpr int HEIGHT = {h};\n\n    enum CellType : uint8_t {{ UNKNOWN=0, FIELD=1, ROAD=2, OBSTACLE=3 }};\n\n    inline const std::vector<uint8_t> GRID = {{\n")
        # write grid as row-major
        lines = []
        for y in range(h):
            row = ', '.join(str(int(v)) for v in grid[y, :])
            lines.append('        ' + row)
        f.write(',\n'.join(lines))
        f.write('\n    };\n\n')
        # write polygons for each class
        for cls in ['FIELD','ROAD','BUILDING','OTHER']:
            key = cls
            polys = polys_by_class.get(key, [])
            varname = key + "S"
            f.write(f"    // Polygons for {cls}\n")
            f.write(f"    inline const std::vector<std::vector<std::pair<int,int>>> {varname} = {{\n")
            poly_lines = []
            for poly in polys:
                pts = ', '.join(f"{{{x}, {y}}}" for (x,y) in poly)
                poly_lines.append(f"        {{ {pts} }}")
            f.write(',\n'.join(poly_lines))
            f.write('\n    };\n\n')
        f.write('} // namespace SegMap\n\n#endif // ' + guard + '\n')


def write_map_class_header(out_path, image_name, scale, grid, map_name=None, map_url=""):
    """
    Write a header that provides an inline factory function returning a Map
    (the class defined in internal-representations/include/Map.h).

    The function is: inline Map createGeneratedMap() { ... }
    It constructs Map(width, height, name, mapUrl) and sets each cell with
    setCell(x,y,value) where value is 0 (accessible) or 1 (inaccessible).
    """
    h, w = grid.shape
    basename = os.path.basename(out_path)
    guard = ("_" + os.path.splitext(basename)[0] + "_MAP_HPP_").upper()
    if map_name is None:
        map_name = os.path.splitext(os.path.basename(image_name))[0]

    with open(out_path, 'w') as f:
        f.write(f"// Generated by segment_and_export.py from {cpp_escape(image_name)}\n")
        f.write(f"// scale={scale}\n\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write('#include "Map.h"\n')
        f.write('#include <string>\n\n')
        f.write('inline Map createGeneratedMap() {\n')
        f.write(f'    Map m({w}, {h}, std::string("{cpp_escape(map_name)}"), std::string("{cpp_escape(map_url)}"));\n')
        f.write('    // 0 = accessible, 1 = inaccessible\n')
        for y in range(h):
            for x in range(w):
                val = int(grid[y, x])
                # convert our convention to Map's: 0 accessible, 1 inaccessible
                # Our grid codes: 0=unknown,1=FIELD,2=ROAD,3=OBSTACLE -> FIELD/ROAD => 0, others => 1
                if val in (1, 2):
                    cell = 0
                else:
                    cell = 1
                f.write(f'    m.setCell({x}, {y}, {cell});\n')
        f.write('    return m;\n')
        f.write('}\n\n#endif // ' + guard + '\n')


def main():
    args = parse_args()
    img, scale = load_and_resize(args.input_image, args.resize)
    labels, centers = cluster_colors(img, k=args.k)
    mapping = classify_clusters(centers)
    # create polygons per class
    polys_by_class = { 'FIELD': [], 'ROAD': [], 'BUILDING': [], 'OTHER': [] }
    for idx in sorted(set(labels.flatten().tolist())):
        mask = mask_for_label(labels, idx)
        mask = postprocess_mask(mask)
        polys = polygons_from_mask(mask, min_area=args.min_area)
        cls = mapping.get(idx, 'OTHER')
        polys_by_class[cls].extend(polys)

    grid = build_occupancy_grid(labels, mapping, args.grid)
    # Pad/dilate road cells in the occupancy grid so roads form continuous lines.
    # We only expand into non-obstacle cells (i.e., avoid turning OBSTACLE=3 into ROAD=2).
    if args.road_pad and args.road_pad > 0:
        try:
            # use OpenCV dilation on a small rectangular kernel
            road_mask = (grid == 2).astype(np.uint8) * 255
            kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 3))
            dilated = cv2.dilate(road_mask, kernel, iterations=args.road_pad)
            dilated_mask = (dilated > 0)
            # only set road where not obstacle
            grid[np.logical_and(dilated_mask, grid != 3)] = 2
        except Exception:
            # If OpenCV operations fail for any reason, fall back to leaving grid unchanged
            pass
    # Optionally write JSON output for server-side consumption
    if args.out_json:
        import json
        outj = {
            "width": int(grid.shape[1]),
            "height": int(grid.shape[0]),
            "grid": grid.tolist()
        }
        with open(args.out_json, 'w') as jf:
            json.dump(outj, jf)

    if args.format == 'map_class':
        # produce a header that contains a factory to create the Map object
        write_map_class_header(args.output_header, args.input_image, scale, grid, map_name=args.map_name, map_url=args.map_url)
        print(f"Wrote Map factory header {args.output_header}: grid {grid.shape[1]}x{grid.shape[0]}")
    else:
        write_cpp_header(args.output_header, args.input_image, scale, polys_by_class, grid)
        print(f"Wrote {args.output_header}: grid {grid.shape[1]}x{grid.shape[0]}, polygons: ")
        for cls in polys_by_class:
            print(f"  {cls}: {len(polys_by_class[cls])} polygons")

if __name__ == '__main__':
    main()
