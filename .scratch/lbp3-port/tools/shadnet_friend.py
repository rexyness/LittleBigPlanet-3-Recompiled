#!/usr/bin/env python3
"""Sends a shadNet friend request (or accepts one: a request back to the sender makes them friends), signed
in as an account from its file (name= / password= lines; the password is never printed).
Our own code; the protocol is shadNet's (Login 0, AddFriend 8; header and framing as in
create_online_account.py).

usage: shadnet_friend.py <account file> <online ID to befriend> [--server shadnet.lbppc.com:31313] [--plain]
Prints the login's friends and requests, then the AddFriend answer.
"""
import argparse
import socket
import ssl
import struct
import sys

HEADER = 15
REQUEST, REPLY, NOTIFICATION, SERVER_INFO = 0, 1, 2, 3
LOGIN, ADD_FRIEND = 0, 8
ERRORS = {0: 'ok', 1: 'malformed', 2: 'invalid', 3: 'invalid input', 5: 'login error', 6: 'already signed in',
          7: 'unknown online name', 8: 'wrong password', 23: 'unauthorized', 26: 'not found', 27: 'blocked',
          28: 'already friends'}


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


def read_varint(b, i):
    n = shift = 0
    while True:
        c = b[i]
        i += 1
        n |= (c & 0x7f) << shift
        shift += 7
        if not c & 0x80:
            return n, i


def fields(b):
    """(field, wire type, value) of a protobuf message, one level deep."""
    i, out = 0, []
    while i < len(b):
        key, i = read_varint(b, i)
        f, t = key >> 3, key & 7
        if t == 0:
            v, i = read_varint(b, i)
        elif t == 2:
            n, i = read_varint(b, i)
            v = b[i:i + n]
            i += n
        elif t == 1:
            v = b[i:i + 8]
            i += 8
        elif t == 5:
            v = b[i:i + 4]
            i += 4
        else:
            raise ValueError('unknown wire type')
        out.append((f, t, v))
    return out


def read_packet(sock):
    def read(count):
        data = b''
        while len(data) < count:
            chunk = sock.recv(count - len(data))
            if not chunk:
                raise RuntimeError('the server closed the connection')
            data += chunk
        return data
    kind, command, total, packet_id = struct.unpack('<BHIQ', read(HEADER))
    return kind, command, packet_id, read(total - HEADER)


def send(sock, command, packet_id, proto):
    payload = struct.pack('<I', len(proto)) + proto
    sock.sendall(struct.pack('<BHIQ', REQUEST, command, HEADER + len(payload), packet_id) + payload)


def wait_reply(sock, command):
    while True:
        kind, cmd, _id, body = read_packet(sock)
        if kind == REPLY and cmd == command:
            return body


def account(path):
    name = password = ''
    for line in open(path, encoding='utf-8'):
        if '=' in line or ':' in line:
            sep = '=' if '=' in line else ':'
            k, v = line.split(sep, 1)
            k, v = k.strip().lower(), v.strip()
            if k in ('name', 'online_name', 'npid', 'username'):
                name = v
            elif k in ('password', 'pass'):
                password = v
    if not name or not password:
        sys.exit(f'{path}: no name or password')
    return name, password


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('account_file')
    ap.add_argument('target')
    ap.add_argument('--server', default='shadnet.lbppc.com:31313')
    ap.add_argument('--plain', action='store_true', help='no TLS (a test server)')
    a = ap.parse_args()
    name, password = account(a.account_file)
    host, port = a.server.rsplit(':', 1)
    sock = socket.create_connection((host, int(port)), timeout=15)
    if not a.plain:
        sock = ssl.create_default_context().wrap_socket(sock, server_hostname=host)
    with sock:
        kind, *_ = read_packet(sock)
        if kind != SERVER_INFO:
            sys.exit('not a shadNet server')
        send(sock, LOGIN, 1, proto_string(1, name) + proto_string(2, password) + proto_string(4, 'CUSA00473'))
        body = wait_reply(sock, LOGIN)
        if not body or body[0]:
            sys.exit(f'{name}: login refused ({ERRORS.get(body[0] if body else 1, body[0] if body else "?")})')
        reply = body[5:5 + struct.unpack('<I', body[1:5])[0]]
        friends, sent, received = [], [], []
        for f, t, v in fields(reply):
            if f == 3 and t == 2:  # friends {npid, online, ...}
                entry = {ff: vv for ff, tt, vv in fields(v)}
                friends.append((entry.get(1, b'').decode(), bool(entry.get(2, 0))))
            elif f == 4 and t == 2:
                sent.append(v.decode())
            elif f == 5 and t == 2:
                received.append(v.decode())
        print(f'{name}: signed in; friends {friends}, requests sent {sent}, received {received}')
        if any(n == a.target for n, _ in friends):
            print(f'{name}: already friends with {a.target}')
            return
        send(sock, ADD_FRIEND, 2, proto_string(1, a.target))
        body = wait_reply(sock, ADD_FRIEND)
        code = body[0] if body else 1
        print(f'{name}: AddFriend {a.target}: {ERRORS.get(code, code)}')


if __name__ == '__main__':
    main()
