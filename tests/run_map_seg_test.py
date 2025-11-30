#!/usr/bin/env python3
"""
Integration test: download/create satellite image, POST map with mapUrl, run segmentation via server, and verify pathfinding works.

This script follows the pattern in tests/run_tests.py: it builds and runs the server,
then exercises the /map/{id} POST (with mapUrl pointing to a local image file),
creates a robot, requests pathfinding, and checks simulation.log for path events.

Note: This test requires the segmentation script's Python dependencies to be
installed (see scripts/requirements.txt). The server runs the segmentation
synchronously when handling the map creation or pathfind request.
"""

import os
import sys
import time
import socket
import subprocess
import uuid
import json

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SERVER_BIN = os.path.join(ROOT, 'agrios_backend')
SIM_LOG = os.path.join(ROOT, 'simulation.log')
PORT = 15003


def wait_for_port(port, timeout=5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.create_connection(('127.0.0.1', port), timeout=0.5)
            s.close()
            return True
        except Exception:
            time.sleep(0.1)
    return False


def get_free_port():
    """Ask the OS for a free TCP port and return it."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
    s.close()
    return port


def http_request(method, path, body=None):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(('127.0.0.1', PORT))
    if body is None:
        req = f"{method} {path} HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n"
    else:
        if isinstance(body, (dict, list)):
            body = json.dumps(body)
        req = f"{method} {path} HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: {len(body.encode('utf-8'))}\r\nConnection: close\r\n\r\n{body}"
    sock.sendall(req.encode('utf-8'))
    resp = b""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            break
        resp += chunk
    sock.close()
    return resp.decode('utf-8', errors='ignore')


def create_test_image(path, width=100, height=100):
    # create a tiny synthetic "satellite" image with:
    # - green background (field)
    # - a 2-pixel vertical white building in the middle (inaccessible)
    try:
        from PIL import Image, ImageDraw
    except Exception:
        print('Pillow required for test; install with pip install pillow')
        sys.exit(1)

    img = Image.new('RGB', (width, height), (30, 160, 40))
    draw = ImageDraw.Draw(img)
    # draw a vertical building at x=4..5
    for x in (4,5):
        for y in range(height):
            draw.point((x,y), fill=(220,220,220))
    img.save(path)


def main():
    # build server if needed
    if not os.path.exists(SERVER_BIN):
        print('Server binary not found, building...')
        rc = subprocess.call(['make', 'build'], cwd=ROOT)
        if rc != 0:
            print('Build failed')
            sys.exit(1)

    # ensure fresh simulation.log
    try:
        if os.path.exists(SIM_LOG):
            os.remove(SIM_LOG)
    except Exception:
        pass

    MAP_IMAGE_URL = "https://api.mapbox.com/styles/v1/mapbox/satellite-v9/static/-94.90440291670808,43.11874590782966,14.24179886497068,0,0/500x500?access_token=pk.eyJ1Ijoid21hcnR1Y2NpIiwiYSI6ImNtaGt2enU1czA5bjEyanFrYmN4emdvejgifQ.p0apnDthn5Ym5wcl_iBPAQ"

    MAP_WIDTH = 500
    MAP_HEIGHT = 500

    if MAP_IMAGE_URL:
        tmp_img = MAP_IMAGE_URL
        created_local = False
        print('Using provided remote image URL:', tmp_img)
    else:
        # create test image locally
        tmp_img = f"/tmp/test_map_img_{uuid.uuid4().hex}.jpg"
        create_test_image(tmp_img, width=MAP_WIDTH, height=MAP_HEIGHT)
        print('Created test image at', tmp_img)
        created_local = True

    # start server
    proc = subprocess.Popen([SERVER_BIN, '--port', str(PORT)], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    try:
        ok = wait_for_port(PORT, timeout=5.0)
        if not ok:
            print('Server did not start in time')
            proc.terminate()
            proc.wait()
            sys.exit(1)

    # create a map with mapUrl pointing to our local image path
        map_id = str(uuid.uuid4())
        map_body = {'width': MAP_WIDTH, 'height': MAP_HEIGHT, 'name': 'testmap', 'mapUrl': tmp_img}
        resp = http_request('POST', f'/map/{map_id}', map_body)
        print('Create map response:')
        print(resp.split('\r\n\r\n',1)[1] if '\r\n\r\n' in resp else resp)
        # After creating the map, also run the segmentation locally for visualization
        # The server will run and remove its temporary JSON file; to ensure we have
        # a copy for the test visuals we run the segmentation script here and write
        # the JSON into tests/visualizations/seg_map_<map_id>.json
        viz_dir = os.path.join(ROOT, 'tests', 'visualizations')
        os.makedirs(viz_dir, exist_ok=True)
        seg_json_local = os.path.join(viz_dir, f"seg_map_{map_id}.json")
        local_for_seg = tmp_img
        downloaded_for_test = False
        if isinstance(tmp_img, str) and tmp_img.lower().startswith('http'):
            # download remote image for local segmentation
            local_for_seg = f"/tmp/test_map_img_{map_id}.img"
            token = os.environ.get('MAPBOX_ACCESS_TOKEN')
            dl_cmd = ['curl', '-s', '-L', '-o', local_for_seg, tmp_img]
            if token:
                dl_cmd.extend(['-H', f'Authorization: Bearer {token}'])
            print('Downloading image for local segmentation:', ' '.join(dl_cmd))
            try:
                rc = subprocess.call(dl_cmd)
                if rc == 0:
                    downloaded_for_test = True
                else:
                    print('Warning: failed to download image for local segmentation, rc=', rc)
            except Exception as ex:
                print('Warning: exception while downloading image for local segmentation:', ex)

        # Run segmentation script to produce JSON used by visualization
        seg_cmd = [sys.executable, os.path.join(ROOT, 'scripts', 'segment_and_export.py'), local_for_seg, os.path.join('/tmp', f'seg_out_{map_id}.hpp'), '--format', 'map_class', '--out-json', seg_json_local, '--grid', str(MAP_WIDTH)]
        print('Running segmentation locally for visualization:', ' '.join(seg_cmd))
        try:
            subprocess.call(seg_cmd)
        except Exception as ex:
            print('Warning: segmentation script failed to run locally:', ex)

        # wait briefly for segmentation to run (server runs segmentation synchronously but allow some time)
        time.sleep(1.0)

        # create a robot positioned at 0,0 assigned to this map
        robot_id = str(uuid.uuid4())
        robot = {
            'name': 'testbot',
            'id': robot_id,
            'type': 'tester',
            'attributes': '',
            'position': [0,0],
            'mapId': map_id
        }
        resp = http_request('POST', '/robots', [robot])
        print('Create robot response:')
        print(resp.split('\r\n\r\n',1)[1] if '\r\n\r\n' in resp else resp)

        # invoke pathfind to target 9,5 (to the right side beyond the building)
        body = {'mapId': map_id, 'target': [9,5]}
        resp = http_request('POST', f'/robots/{robot_id}/pathfind', body)
        print('Pathfind response:')
        print(resp.split('\r\n\r\n',1)[1] if '\r\n\r\n' in resp else resp)

        # wait for simulation.log to be written
        deadline = time.time() + 5.0
        while time.time() < deadline:
            if os.path.exists(SIM_LOG):
                break
            time.sleep(0.1)

        if not os.path.exists(SIM_LOG):
            print('simulation.log not created')
            sys.exit(1)

        # Check simulation.log for PathReconstructed or MoveExecuted
        with open(SIM_LOG, 'r') as f:
            content = f.read()
        print('\n--- simulation.log ---')
        print(content)

        if 'PATH_RECONSTRUCTED' in content or 'MOVE_EXECUTED' in content:
            print('Test succeeded: pathfinding produced a path')
            rc = 0
        else:
            print('Test failed: no pathing events found')
            rc = 2

        # Attempt to visualize results using the local segmentation JSON we produced earlier
        try:
            if os.path.exists(seg_json_local):
                out_prefix = os.path.join(viz_dir, f"map_vis_{map_id}")
                viz_cmd = [sys.executable, os.path.join(ROOT, 'scripts', 'visualize_map_and_path.py'), '--map-json', seg_json_local, '--sim-log', SIM_LOG, '--out-prefix', out_prefix, '--cell-size', '24']
                print('Running visualization:', ' '.join(viz_cmd))
                subprocess.call(viz_cmd)
                print('Visualization outputs:')
                for ext in ['_map.png', '_path.png', '_path.gif']:
                    p = out_prefix + ext
                    if os.path.exists(p):
                        print('  ', p)
                    else:
                        print('  missing:', p)
            else:
                print('Segmentation JSON not found for visualization:', seg_json_local)
        except Exception as ex:
            print('Visualization failed:', ex)
        finally:
            # cleanup any downloaded temporary image we created for the local segmentation
            try:
                if downloaded_for_test and os.path.exists(local_for_seg):
                    os.remove(local_for_seg)
            except Exception:
                pass

    finally:
        try:
            proc.terminate()
            proc.wait(timeout=2)
        except Exception:
            pass
        # cleanup tmp image if we created it locally
        try:
            if created_local and os.path.exists(tmp_img):
                os.remove(tmp_img)
        except Exception:
            pass

    sys.exit(rc)

if __name__ == '__main__':
    main()
