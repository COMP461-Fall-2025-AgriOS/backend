#!/usr/bin/env python3
"""
Visualize a map JSON and simulation.log; produce PNG of map + path overlay and GIF of robot movement.

Usage:
  python3 scripts/visualize_map_and_path.py --map-json /tmp/my_map.json --sim-log simulation.log --out-prefix /tmp/my_map_vis

Outputs:
  - /tmp/my_map_vis_map.png        : static map image
  - /tmp/my_map_vis_path.png       : static map with path overlay
  - /tmp/my_map_vis_path.gif       : animated robot movement GIF

Map JSON format expected:
  { "width": <w>, "height": <h>, "grid": [[row0...], [row1...], ...] }

Simulation log is parsed for lines containing PATH (reconstructed) or MOVE_EXECUTED entries.
"""

import argparse
import json
import os
import re
from PIL import Image, ImageDraw
import imageio


def parse_args():
    p = argparse.ArgumentParser(description='Visualize map JSON and simulation log')
    p.add_argument('--map-json', required=True, help='Path to map JSON produced by segmentation script')
    p.add_argument('--sim-log', default='simulation.log', help='Path to simulation.log')
    p.add_argument('--out-prefix', default='/tmp/map_vis', help='Output prefix for generated files')
    p.add_argument('--cell-size', type=int, default=24, help='Pixels per grid cell')
    p.add_argument('--robot-id', default='', help='Optional robot id to visualize (if not provided first PATH or MOVE_EXECUTED is used)')
    return p.parse_args()


def load_map(json_path):
    with open(json_path, 'r') as f:
        data = json.load(f)
    width = int(data['width'])
    height = int(data['height'])
    grid = data['grid']
    return width, height, grid


def render_map_image(width, height, grid, cell_size=24):
    img_w = width * cell_size
    img_h = height * cell_size
    img = Image.new('RGB', (img_w, img_h), (255,255,255))
    draw = ImageDraw.Draw(img)

    # color mapping for grid codes emitted by segmentation script:
    # 0 = UNKNOWN, 1 = FIELD, 2 = ROAD, 3 = OBSTACLE
    color_for = {
        0: (200, 200, 200), # unknown - light gray
        1: (50, 160, 50),   # field - green
        2: (128, 128, 128), # road - grey
        3: (30, 30, 30),    # obstacle/building - dark
    }

    for y in range(height):
        for x in range(width):
            code = int(grid[y][x])
            c = color_for.get(code, (200,200,200))
            x0 = x * cell_size
            y0 = y * cell_size
            draw.rectangle([x0, y0, x0 + cell_size - 1, y0 + cell_size - 1], fill=c)

    # draw grid lines
    for x in range(width + 1):
        draw.line([(x*cell_size,0),(x*cell_size,img_h)], fill=(80,80,80), width=1)
    for y in range(height + 1):
        draw.line([(0,y*cell_size),(img_w,y*cell_size)], fill=(80,80,80), width=1)

    return img


def parse_simulation_log(sim_log_path):
    # returns dict robot_id -> {'path': [(x,y),...], 'moves': [(x,y),...]} where path is reconstructed path
    robots = {}
    if not os.path.exists(sim_log_path):
        return robots

    path_re = re.compile(r'PATH size=\d+ coords=\(([^)]+)\)(?:;\(([^)]+)\))*')
    # We'll parse PATH lines manually
    with open(sim_log_path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line: continue
            # extract robotId
            m_id = re.search(r'robotId="([^"]+)"', line)
            if not m_id:
                continue
            rid = m_id.group(1)
            if rid not in robots:
                robots[rid] = {'path': [], 'moves': []}

            if 'PATH' in line and 'coords=' in line:
                # coords follow like coords=(x,y);(x2,y2);...
                coords_part = line.split('coords=',1)[1]
                coords = []
                for pair in coords_part.split(';'):
                    pair = pair.strip()
                    if pair.startswith('(') and pair.endswith(')'):
                        try:
                            x,y = pair[1:-1].split(',')
                            coords.append( (int(x), int(y)) )
                        except Exception:
                            pass
                if coords:
                    robots[rid]['path'] = coords
            elif 'MOVE_EXECUTED' in line:
                # parse x= y=
                mx = re.search(r'x=(\d+)', line)
                my = re.search(r'y=(\d+)', line)
                if mx and my:
                    robots[rid]['moves'].append((int(mx.group(1)), int(my.group(1))))
    return robots


def overlay_path(img, path, cell_size=24, robot_color=(255,0,0)):
    base = img.copy()
    draw = ImageDraw.Draw(base)
    # Draw path as circles and lines
    pts = [((x*cell_size + cell_size//2), (y*cell_size + cell_size//2)) for (x,y) in path]
    if len(pts) >= 2:
        draw.line(pts, fill=(255,200,0), width=max(1, cell_size//6))
    for p in pts:
        r = max(2, cell_size//4)
        draw.ellipse([p[0]-r, p[1]-r, p[0]+r, p[1]+r], fill=robot_color)
    return base


def make_gif_frames(base_img, moves, cell_size=24, robot_color=(255,0,0)):
    frames = []
    # start with base image
    blank = base_img.copy()
    draw = ImageDraw.Draw(blank)
    frames.append(blank.copy())

    for pos in moves:
        img = base_img.copy()
        draw = ImageDraw.Draw(img)
        cx = pos[0]*cell_size + cell_size//2
        cy = pos[1]*cell_size + cell_size//2
        r = max(2, cell_size//3)
        draw.ellipse([cx-r, cy-r, cx+r, cy+r], fill=robot_color)
        frames.append(img)
    return frames


def main():
    args = parse_args()
    width, height, grid = load_map(args.map_json)
    base = render_map_image(width, height, grid, cell_size=args.cell_size)

    out_map = args.out_prefix + '_map.png'
    base.save(out_map)
    print('Saved map image to', out_map)

    robots = parse_simulation_log(args.sim_log)
    if not robots:
        print('No robot events found in', args.sim_log)
        return

    # pick robot id
    rid = args.robot_id if args.robot_id else next(iter(robots.keys()))
    if rid not in robots:
        print('Requested robot id not found in log; available:', list(robots.keys()))
        return

    data = robots[rid]
    path = data.get('path', [])
    moves = data.get('moves', [])

    if path:
        path_img = overlay_path(base, path, cell_size=args.cell_size)
        out_path_img = args.out_prefix + '_path.png'
        path_img.save(out_path_img)
        print('Saved path overlay to', out_path_img)

    if moves:
        frames = make_gif_frames(base, moves, cell_size=args.cell_size)
        out_gif = args.out_prefix + '_path.gif'
        # save gif with imageio
        imgs = [frame.convert('P', palette=Image.ADAPTIVE) for frame in frames]
        imageio.mimsave(out_gif, [imageio.core.util.Array(frame) for frame in imgs], duration=0.4)
        print('Saved animated GIF to', out_gif)
    else:
        print('No MOVE_EXECUTED events to animate for robot', rid)


if __name__ == '__main__':
    main()
