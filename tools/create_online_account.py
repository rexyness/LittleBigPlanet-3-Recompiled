#!/usr/bin/env python3
"""Creates the player's two online accounts (same name and password) on a community server:
shadNet (sign-in, friends, multiplayer) and Refresh (LittleBigPlanet's levels and profiles).
Our own code; the protocol facts (shadNet's Create command, Refresh's web API calls) are from LBPPC's docs and
scripts/online_accounts.py (https://github.com/emurphy04/LBPPC, aad6a91), which LBPPC's launcher uses for the same.

usage: create_online_account.py <online name> [--server play.lbppc.com] [--root D:\\Recomps\\lbp3-recompiled]
A random password is made, saved to <root>\\user\\config\\online_account.txt BEFORE anything is sent (so it isn't
lost if a step fails), and shown once. Only a SHA-512 of it goes to Refresh; shadNet receives it over TLS.
"""
import argparse
import hashlib
import json
import re
import secrets
import socket
import ssl
import struct
import sys
import urllib.error
import urllib.request
from pathlib import Path

NAME = re.compile(r'[A-Za-z0-9_-]{3,16}')
OFFICIAL = {'refresh': 'https://play.lbppc.com', 'shadnet': ('shadnet.lbppc.com', 31313, True)}
HEADER = 15  # type u8, command u16, total size u32, packet id u64 (little-endian)
REQUEST, REPLY, SERVER_INFO, CREATE = 0, 1, 3, 2
SHADNET_ERRORS = {1: 'the request was malformed', 2: 'the request was invalid',
                  3: 'the name, password or email is not accepted', 4: 'too many attempts, try again in a minute',
                  10: 'the server could not create it', 11: 'that online name is taken',
                  12: 'that email provider is not allowed'}


def varint(n):
    out = bytearray()
    while True:
        b = n & 0x7f
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def proto_string(field, text):
    data = text.encode('utf-8')
    return bytes([field << 3 | 2]) + varint(len(data)) + data


def read_packet(sock):
    def read(count):
        data = b''
        while len(data) < count:
            chunk = sock.recv(count - len(data))
            if not chunk:
                raise RuntimeError('the shadNet server closed the connection')
            data += chunk
        return data
    kind, command, total, packet_id = struct.unpack('<BHIQ', read(HEADER))
    return kind, command, packet_id, read(total - HEADER)


def create_shadnet(host, port, tls, name, password, email):
    request = proto_string(1, name) + proto_string(2, password) + proto_string(4, email)
    payload = struct.pack('<I', len(request)) + request
    sock = socket.create_connection((host, port), timeout=15)
    if tls:
        sock = ssl.create_default_context().wrap_socket(sock, server_hostname=host)
    with sock:
        kind, *_ = read_packet(sock)
        if kind != SERVER_INFO:
            raise RuntimeError(f'{host}:{port} does not look like a shadNet server')
        sock.sendall(struct.pack('<BHIQ', REQUEST, CREATE, HEADER + len(payload), 1) + payload)
        while True:
            kind, command, _id, body = read_packet(sock)
            if kind == REPLY and command == CREATE:
                break
    code = body[0] if body else 1
    if code:
        raise RuntimeError('shadNet: ' + SHADNET_ERRORS.get(code, f'error {code}'))


def call(url, body, token=None, method='POST'):
    headers = {'Content-Type': 'application/json'}
    if token:
        headers['Authorization'] = token
    req = urllib.request.Request(url, data=json.dumps(body).encode(), method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=20, context=ssl.create_default_context()) as reply:
            return json.loads(reply.read() or b'{}')
    except urllib.error.HTTPError as error:
        try:
            return json.loads(error.read() or b'{}')
        except ValueError:
            return {'success': False, 'error': f'HTTP {error.code}'}


def create_refresh(server, name, password, email):
    api = server.rstrip('/') + '/api/v3'
    reply = call(f'{api}/register', {'username': name, 'emailAddress': email,
                                     'passwordSha512': hashlib.sha512(password.encode()).hexdigest()})
    if not reply.get('success'):
        raise RuntimeError('Refresh: ' + str(reply.get('error') or 'registration failed'))
    token = reply['data']['tokenData']
    settings = call(f'{api}/users/me', {'psnAuthenticationAllowed': True, 'rpcnAuthenticationAllowed': True},
                    token, 'PATCH')
    if not settings.get('success'):
        raise RuntimeError('Refresh: registered, but game logins are not allowed yet: '
                           + str(settings.get('error', settings)))


def main():
    p = argparse.ArgumentParser()
    p.add_argument('name')
    p.add_argument('--server', default='play.lbppc.com')
    p.add_argument('--root', default=r'D:\Recomps\lbp3-recompiled')
    a = p.parse_args()
    if not NAME.fullmatch(a.name):
        sys.exit('The online name needs 3 to 16 letters, digits, - or _.')
    if a.server.strip().lower().rstrip('/') not in ('play.lbppc.com', 'https://play.lbppc.com'):
        sys.exit('Only play.lbppc.com is set up here.')
    password = secrets.token_urlsafe(18)
    email = f'{a.name.lower()}@lbppc.invalid'
    out = Path(a.root) / 'user' / 'config' / 'online_account.txt'
    if out.exists():
        sys.exit(f'{out} already exists; not overwriting an account file.')
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(f'name={a.name}\npassword={password}\nserver={a.server}\n', encoding='utf-8')
    print(f'saved {out}')
    host, port, tls = OFFICIAL['shadnet']
    try:
        create_shadnet(host, port, tls, a.name, password, email)
        print('shadNet account: created')
    except (RuntimeError, OSError) as error:
        sys.exit(f'shadNet account: NOT created ({error}). Nothing was sent to Refresh.')
    try:
        create_refresh(OFFICIAL['refresh'], a.name, password, email)
        print('Refresh account: created, game logins allowed')
    except (RuntimeError, OSError, KeyError, ValueError) as error:
        sys.exit(f'Refresh account: NOT created ({error}). The shadNet account exists with the saved password.')
    print(f'Both accounts are ready for "{a.name}". The password is in {out}.')


if __name__ == '__main__':
    main()
