#!/usr/bin/env python3
"""A stand-in for GitHub's releases API, for the launcher's end-to-end update test (never shipped).

usage: fake_release_server.py <folder> <tag> [--port N] [--mode good|badhash|nosha|fail|truncate]
  <folder> holds LBP3-Recompiled-<tag>.zip and its .sha256 (tools/make_package.ps1 -Version <tag>).
  Listens on 127.0.0.1 only and prints "port N". Start the launcher with
  LBP3_UPDATE_TEST_API=http://127.0.0.1:N (the launcher takes only that loopback form).

GET /repos/<owner>/<repo>/releases/latest  the release JSON, with the zip and .sha256 as assets
GET /dl/<file>                             the asset, as the mode says:
    good      as it is
    badhash   the .sha256 names another hash
    nosha     the release lists no .sha256
    fail      the zip download answers HTTP 500
    truncate  the zip download stops halfway (Content-Length says the full size)
GET /mode/<mode>                           switches the mode (the test steps through them)
Each request is logged to stdout.
"""
import argparse
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MODES = ('good', 'badhash', 'nosha', 'fail', 'truncate')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('folder')
    p.add_argument('tag')
    p.add_argument('--port', type=int, default=0)
    p.add_argument('--mode', default='good', choices=MODES)
    a = p.parse_args()
    zip_name = f'LBP3-Recompiled-{a.tag}.zip'
    zip_path = os.path.join(a.folder, zip_name)
    if not os.path.isfile(zip_path) or not os.path.isfile(zip_path + '.sha256'):
        sys.exit(f'{zip_path} or its .sha256 is missing')
    state = {'mode': a.mode}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            print(f'[{state["mode"]}] {self.command} {self.path} -> {args[1] if len(args) > 1 else ""}', flush=True)

        def send(self, code, body, ctype='application/json', length=None):
            self.send_response(code)
            self.send_header('Content-Type', ctype)
            self.send_header('Content-Length', str(len(body) if length is None else length))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            port = self.server.server_address[1]
            if self.path.startswith('/mode/'):
                mode = self.path[len('/mode/'):]
                if mode not in MODES:
                    return self.send(400, b'{"message":"no such mode"}')
                state['mode'] = mode
                return self.send(200, json.dumps({'mode': mode}).encode())
            if self.path.endswith('/releases/latest'):
                assets = [{'name': zip_name, 'size': os.path.getsize(zip_path),
                           'browser_download_url': f'http://127.0.0.1:{port}/dl/{zip_name}'}]
                if state['mode'] != 'nosha':
                    assets.append({'name': zip_name + '.sha256',
                                   'browser_download_url': f'http://127.0.0.1:{port}/dl/{zip_name}.sha256'})
                body = {'tag_name': a.tag, 'name': f'Test release {a.tag}', 'body': 'A fake release for the updater test.',
                        'published_at': '2026-11-13T12:00:00Z', 'assets': assets}
                return self.send(200, json.dumps(body).encode())
            if self.path == f'/dl/{zip_name}':
                data = open(zip_path, 'rb').read()
                if state['mode'] == 'fail':
                    return self.send(500, b'{"message":"server error"}')
                if state['mode'] == 'truncate':
                    self.send_response(200)
                    self.send_header('Content-Type', 'application/octet-stream')
                    self.send_header('Content-Length', str(len(data)))
                    self.end_headers()
                    self.wfile.write(data[:len(data) // 2])
                    self.wfile.flush()
                    self.close_connection = True
                    return
                return self.send(200, data, 'application/octet-stream')
            if self.path == f'/dl/{zip_name}.sha256':
                text = open(zip_path + '.sha256', 'rb').read()
                if state['mode'] == 'badhash':
                    text = ('0' * 64 + '  ' + zip_name + '\n').encode()
                return self.send(200, text, 'text/plain')
            return self.send(404, b'{"message":"Not Found"}')

    server = ThreadingHTTPServer(('127.0.0.1', a.port), Handler)
    print(f'port {server.server_address[1]}', flush=True)
    server.serve_forever()


if __name__ == '__main__':
    main()
