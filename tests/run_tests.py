#!/usr/bin/env python3
import subprocess
import time
import os
import socket
import sys
import signal
import json

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SERVER_BIN = os.path.join(ROOT, 'agrios_backend')
SIM_LOG = os.path.join(ROOT, 'simulation.log')

PORT = 9090

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


def main():
    if not os.path.exists(SERVER_BIN):
        print("Server binary not found, building...")
        rc = subprocess.call(['make', 'build'], cwd=ROOT)
        if rc != 0:
            print("Build failed")
            sys.exit(1)

    # ensure fresh simulation.log
    try:
        if os.path.exists(SIM_LOG):
            os.remove(SIM_LOG)
    except Exception:
        pass

    # start server
    env = os.environ.copy()
    # run with explicit port
    proc = subprocess.Popen([SERVER_BIN, '--port', str(PORT)], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    try:
        ok = wait_for_port(PORT, timeout=5.0)
        if not ok:
            print('Server did not start in time')
            proc.terminate()
            proc.wait()
            sys.exit(1)

        # create a map
        import uuid
        map_id = str(uuid.uuid4())
        map_body = {'width': 10, 'height': 10}
        resp = http_request('POST', f'/map/{map_id}', map_body)
        print('Create map response:')
        print(resp.split('\r\n\r\n',1)[1] if '\r\n\r\n' in resp else resp)

        # create a robot positioned at 0,0
        robot_id = str(uuid.uuid4())
        robot = {
            'name': 'testbot',
            'id': robot_id,
            'type': 'tester',
            'attributes': '',
            'position': [0,0]
        }
        resp = http_request('POST', '/robots', [robot])
        print('Create robot response:')
        print(resp.split('\r\n\r\n',1)[1] if '\r\n\r\n' in resp else resp)

        # invoke pathfind to target 5,5
        body = {'mapId': map_id, 'target': [5,5]}
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

        print('\n--- simulation.log ---')
        with open(SIM_LOG, 'r') as f:
            print(f.read())

    finally:
        try:
            proc.send_signal(signal.SIGINT)
            proc.terminate()
        except Exception:
            pass
        proc.wait()

if __name__ == '__main__':
    main()
