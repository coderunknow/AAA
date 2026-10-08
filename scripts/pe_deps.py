#!/usr/bin/env python3
"""Print the DLLs a Windows PE image imports, and fail on non-system runtimes.

Used by the release pipeline as the authoritative Windows dependency audit
(PROMPT 9.6: "verify no unexpected non-system runtime dependency remains").

`dumpbin /dependents` is the canonical tool for this, but on the MSYS bash that
GitHub's Windows runners use it is not reliable: a step that shells out to it
has been observed to abort with an opaque shell status (157) before producing
any output, which makes the audit un-diagnosable and blocks the release on
something unrelated to the artifact.  `dumpbin` output is still captured as
human-readable evidence; this script is what actually decides pass/fail, by
reading the very data `dumpbin` prints -- the PE import directory itself.

The parser is deliberately dependency-free and total: any malformed image is
reported as an error with a reason, never as a silent pass.

Usage:
  python3 scripts/pe_deps.py <image.exe>            # audit; exit 1 on violation
  python3 scripts/pe_deps.py --list <image.exe>     # print the imports; always 0
  python3 scripts/pe_deps.py --self-test            # parser check on a built PE
"""

from __future__ import annotations

import re
import struct
import sys

# A dependency is "system" if Windows itself ships it.  Anything a Mistpine
# build drags in from the toolchain (SDL, bgfx, a MinGW runtime DLL or an MSVC
# redistributable) is not: the artifact has to run on a clean machine.
FORBIDDEN = re.compile(
    r"^(sdl\d*|bgfx|bx|bimg|fcpp|freetype|glfw|"
    r"libgcc_s_.*|libstdc\+\+-.*|libwinpthread-.*|libgomp-.*|libssp-.*|"
    r"msvcp\d+|msvcr\d+|vcruntime\d*|ucrtbase|vcomp\d*|concrt\d*|mfc\d*|"
    r"api-ms-win-crt-.*)",
    re.IGNORECASE,
)

# Modules that are part of the OS and therefore always present.
SYSTEM_HINT = (
    "kernel32", "user32", "gdi32", "advapi32", "shell32", "ole32", "oleaut32",
    "oleacc", "comctl32", "comdlg32", "shlwapi", "version", "winmm", "imm32",
    "setupapi", "cfgmgr32", "ws2_32", "winspool", "msvcrt", "ntdll", "d3d11",
    "d3d12", "dxgi", "opengl32", "userenv", "bcrypt", "crypt32", "secur32",
    "propsys", "uxtheme", "dwmapi", "powrprof", "sensapi", "iphlpapi",
    "netapi32", "psapi", "winhttp", "wininet", "dbghelp", "hid", "dinput8",
    "xinput", "avrt", "msimg32", "winsta", "wtsapi32", "cabinet", "msi",
    "rpcrt4", "ncrypt", "sspicli", "authz", "wldap32", "dnsapi", "pdh",
    "wer", "esent", "mswsock", "normaliz", "sechost",
)


class PEError(Exception):
    pass


def _u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def _u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def _u64(b, o):
    return struct.unpack_from("<Q", b, o)[0]


class Section:
    __slots__ = ("name", "vsize", "vaddr", "rawsize", "rawoff")


def _sections(data: bytes, n: int, off: int) -> list[Section]:
    out = []
    for i in range(n):
        o = off + 40 * i
        s = Section()
        s.name = data[o:o + 8].rstrip(b"\x00").decode("ascii", "replace")
        s.vsize = _u32(data, o + 8)
        s.vaddr = _u32(data, o + 12)
        s.rawsize = _u32(data, o + 16)
        s.rawoff = _u32(data, o + 20)
        out.append(s)
    return out


def _rva_to_off(sections: list[Section], rva: int) -> int:
    for s in sections:
        if s.vaddr <= rva < s.vaddr + max(s.vsize, s.rawsize):
            delta = rva - s.vaddr
            if delta >= s.rawsize:
                # Uninitialised tail (e.g. .bss) -- no backing bytes.
                raise PEError(f"RVA 0x{rva:x} falls past the raw data of {s.name}")
            return s.rawoff + delta
    raise PEError(f"RVA 0x{rva:x} is outside every section")


def _cstring(data: bytes, off: int) -> str:
    end = data.find(b"\x00", off)
    if end < 0:
        raise PEError("unterminated string in image")
    return data[off:end].decode("ascii", "replace")


def _import_dir(data, sections, rva, size, limit=4096):
    """Yield (dll_name) from an import directory table."""
    if rva == 0 or size == 0:
        return
    off = _rva_to_off(sections, rva)
    for _ in range(limit):
        desc = data[off:off + 20]
        if len(desc) < 20:
            raise PEError("truncated import descriptor")
        if desc == b"\x00" * 20:
            return
        name_rva = _u32(desc, 12)
        if name_rva == 0:
            return
        yield _cstring(data, _rva_to_off(sections, name_rva))
        off += 20
    raise PEError("import descriptor table did not terminate")


def imports(path: str) -> tuple[list[str], list[str]]:
    """Return (imports, delay_imports) for a PE image."""
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise PEError("not a PE image (no MZ signature)")
    pe = _u32(data, 0x3C)
    if pe + 24 > len(data) or data[pe:pe + 4] != b"PE\x00\x00":
        raise PEError("not a PE image (no PE signature)")

    coff = pe + 4
    nsec = _u16(data, coff + 2)
    opt_size = _u16(data, coff + 16)
    opt = coff + 20
    if opt_size < 96:
        raise PEError("optional header too small")
    magic = _u16(data, opt)
    if magic == 0x10B:
        nrva_off, pe32plus = opt + 96, False
    elif magic == 0x20B:
        nrva_off, pe32plus = opt + 112, True
    else:
        raise PEError(f"unknown optional header magic 0x{magic:x}")

    nsec_off = opt + opt_size
    if nsec == 0 or nsec > 96:
        raise PEError(f"implausible section count {nsec}")
    sections = _sections(data, nsec, nsec_off)

    nrva = _u32(data, nrva_off)
    dir_off = nrva_off + 4
    dirs = {}
    for i in range(min(nrva, 16)):
        o = dir_off + 8 * i
        dirs[i] = (_u32(data, o), _u32(data, o + 4))

    imp = list(_import_dir(data, sections, *dirs.get(1, (0, 0))))
    delay = list(_import_dir(data, sections, *dirs.get(13, (0, 0))))
    _ = pe32plus  # only the directory offsets differ; both were read above.
    return imp, delay


def _build_fake_pe(dll_names, delay_names=()):
    """Construct a minimal but structurally valid PE64 for the self-test."""
    sec_va, sec_rawoff, align = 0x1000, 0x400, 0x1000
    strings = bytearray()
    offs = {}

    def add(name):
        offs[name] = len(strings)
        strings.extend(name.encode() + b"\x00")

    for n in list(dll_names) + list(delay_names):
        add(n)

    def descs(names):
        out = bytearray()
        for n in names:
            out += struct.pack("<IIIII", 0, 0, 0, 0, 0)
        out += b"\x00" * 20
        return bytes(out)

    imp = bytearray(descs(dll_names))
    delay = bytearray(descs(delay_names)) if delay_names else bytearray()

    # Name RVAs can only be filled in once the string blob's RVA is known.
    def fill(buf, names):
        for i, n in enumerate(names):
            struct.pack_into("<I", buf, 20 * i + 12, sec_va + 16 + len(imp)
                             + len(delay) + ((-(16 + len(imp) + len(delay))) % stride)
                             + offs[n])

    stride = 8
    fill(imp, dll_names)
    fill(delay, delay_names)
    imp, delay = bytes(imp), bytes(delay)

    imp_va = sec_va + 16
    delay_va = imp_va + len(imp)
    str_va = sec_va + 16 + len(imp) + len(delay)
    str_va += (-str_va) % stride

    body = bytearray()
    body += b"\x00" * 16
    body += imp
    body += delay
    body += b"\x00" * (str_va - (sec_va + len(body)))
    body += strings
    raw = bytes(body)
    raw += b"\x00" * ((-len(raw)) % 0x200)

    hdr = bytearray(0x400)
    hdr[0:2] = b"MZ"
    struct.pack_into("<I", hdr, 0x3C, 0x80)
    hdr[0x80:0x84] = b"PE\x00\x00"
    coff = 0x84
    struct.pack_into("<H", hdr, coff + 2, 1)          # NumberOfSections
    struct.pack_into("<H", hdr, coff + 16, 240)       # SizeOfOptionalHeader
    opt = coff + 20
    struct.pack_into("<H", hdr, opt, 0x20B)           # PE32+
    struct.pack_into("<I", hdr, opt + 112, 16)        # NumberOfRvaAndSizes
    d = opt + 116
    struct.pack_into("<II", hdr, d + 8 * 1, imp_va, len(imp))
    if delay_names:
        struct.pack_into("<II", hdr, d + 8 * 13, delay_va, len(delay))
    s = opt + 240
    hdr[s:s + 8] = b".rdata\x00\x00"
    struct.pack_into("<IIII", hdr, s + 8, len(raw), sec_va, len(raw), sec_rawoff)
    return bytes(hdr) + raw


def self_test() -> int:
    import tempfile
    import os

    cases = [
        (["KERNEL32.dll", "msvcrt.dll", "USER32.dll"], [], False),
        (["KERNEL32.dll", "SDL3.dll"], [], True),
        (["KERNEL32.dll", "VCRUNTIME140.dll"], [], True),
        (["KERNEL32.dll", "libwinpthread-1.dll"], [], True),
        (["KERNEL32.dll"], ["bgfx.dll"], True),
    ]
    failures = 0
    with tempfile.TemporaryDirectory() as td:
        for i, (imp, delay, should_fail) in enumerate(cases):
            p = os.path.join(td, f"t{i}.exe")
            with open(p, "wb") as fh:
                fh.write(_build_fake_pe(imp, delay))
            try:
                got_imp, got_delay = imports(p)
            except Exception as exc:  # noqa: BLE001
                print(f"self-test {i}: parser raised {exc}")
                failures += 1
                continue
            if sorted(got_imp) != sorted(imp) or sorted(got_delay) != sorted(delay):
                print(f"self-test {i}: got {got_imp}/{got_delay}, want {imp}/{delay}")
                failures += 1
                continue
            bad = [n for n in got_imp + got_delay if FORBIDDEN.match(n)]
            if bool(bad) != should_fail:
                print(f"self-test {i}: expected violation={should_fail}, bad={bad}")
                failures += 1
    print(f"self-test: {'FAIL' if failures else 'ok'} ({len(cases)} cases)")
    return 1 if failures else 0


def main(argv: list[str]) -> int:
    args = [a for a in argv[1:]]
    list_only = "--list" in args
    args = [a for a in args if a != "--list"]
    if args and args[0] == "--self-test":
        return self_test()
    if len(args) != 1:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    path = args[0]
    try:
        imp, delay = imports(path)
    except (PEError, OSError, struct.error) as exc:
        print(f"pe_deps: cannot read imports of {path}: {exc}", file=sys.stderr)
        # A failure to *read* the image is a failure of the audit, not a pass.
        return 1
    if not imp and not delay:
        print(f"pe_deps: {path} imports no DLLs at all (suspicious)", file=sys.stderr)
        return 1

    print(f"{path}: {len(imp)} import(s), {len(delay)} delay-load import(s)")
    for n in sorted(imp):
        tag = "system" if n.lower().split(".")[0] in SYSTEM_HINT else "REVIEW"
        print(f"  IMPORT  {n}  [{tag}]")
    for n in sorted(delay):
        print(f"  DELAY   {n}  [REVIEW]")

    bad = [n for n in sorted(set(imp) | set(delay)) if FORBIDDEN.match(n)]
    if bad:
        print("pe_deps: unexpected non-system runtime dependency found:", file=sys.stderr)
        for n in bad:
            print(f"  {n}", file=sys.stderr)
        if list_only:
            return 0
        return 1
    print("pe_deps: no unexpected non-system runtime dependency")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
