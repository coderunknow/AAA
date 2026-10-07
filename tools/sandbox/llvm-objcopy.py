#!/usr/bin/env python3
"""Sandbox stand-in for llvm-objcopy on WebAssembly files (Zig's objcopy is ELF-only).

Implements exactly what Emscripten 4.0.x uses:
  llvm-objcopy IN [OUT] --remove-section=GLOB ...      (strip custom sections)
  llvm-objcopy --add-section NAME=FILE IN              (append a custom section)
"""
import fnmatch
import sys


def read_leb(buf, pos):
    result = shift = 0
    while True:
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            return result, pos


def leb(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def main(argv):
    remove, add, files = [], [], []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a.startswith('--remove-section='):
            remove.append(a.split('=', 1)[1])
        elif a == '--remove-section':
            i += 1; remove.append(argv[i])
        elif a == '--add-section':
            i += 1; add.append(argv[i])
        elif a.startswith('--add-section='):
            add.append(a.split('=', 1)[1])
        elif a == '--version':
            print('llvm-objcopy (sandbox wasm stand-in) 21.1.0'); return 0
        elif a.startswith('-'):
            sys.stderr.write(f'llvm-objcopy stub: unsupported option {a}\n'); return 1
        else:
            files.append(a)
        i += 1
    if not files:
        sys.stderr.write('llvm-objcopy stub: no input\n'); return 1
    infile = files[0]
    outfile = files[1] if len(files) > 1 else infile
    data = open(infile, 'rb').read()
    if data[:4] != b'\0asm':
        sys.stderr.write('llvm-objcopy stub: only wasm inputs are supported\n'); return 1
    out = bytearray(data[:8])
    pos = 8
    while pos < len(data):
        sid = data[pos]
        size, body = read_leb(data, pos + 1)
        end = body + size
        keep = True
        if sid == 0:
            nlen, npos = read_leb(data, body)
            name = data[npos:npos + nlen].decode('utf-8', 'replace')
            keep = not any(fnmatch.fnmatchcase(name, pat) for pat in remove)
        if keep:
            out += data[pos:end]
        pos = end
    for spec in add:
        name, path = spec.split('=', 1)
        payload = leb(len(name.encode())) + name.encode() + open(path, 'rb').read()
        out += b'\0' + leb(len(payload)) + payload
    open(outfile, 'wb').write(out)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
