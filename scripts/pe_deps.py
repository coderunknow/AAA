#!/usr/bin/env python3
"""Print the DLLs a Windows PE image imports, and fail on non-system runtimes.

Used by the release pipeline as the authoritative Windows dependency audit
(PROMPT 9.6: "verify no unexpected non-system runtime dependency remains").

`dumpbin /dependents` is the canonical tool for this, but on the MSYS bash that
GitHub's Windows runners use it is not reliable: a step that shells out to it
aborted twice with an opaque shell status (157) before producing any output,
which makes the audit un-diagnosable and blocks the release on something
unrelated to the artifact.  `dumpbin` output is still captured as
human-readable evidence; this script is what actually decides pass/fail, by
reading the very data `dumpbin` prints -- the PE import directory itself.

Offsets follow the Microsoft PE and COFF Specification (optional header,
image-only fields).  They are cross-checked two ways, because an early revision
of this file placed NumberOfRvaAndSizes four bytes late in *both* layouts and
silently reported "imports no DLLs at all" for a healthy 64-bit executable:

  * the standard optional-header sizes are exact:
        96 + 16*8 == 224  (PE32)     112 + 16*8 == 240  (PE32+)
    and both are asserted at import time;
  * the parser refuses to treat a zero or implausible NumberOfRvaAndSizes as
    "no imports" -- it raises instead.

The parser is otherwise dependency-free and total: any malformed image is
reported as an audit failure with a reason, never as a silent pass.

Usage:
  python3 scripts/pe_deps.py <image.exe>            # audit; exit 1 on violation
  python3 scripts/pe_deps.py --list <image.exe>     # print imports; always exit 0
  python3 scripts/pe_deps.py --self-test            # parser check on built PEs
"""

from __future__ import annotations

import re
import struct
import sys

# Optional-header offsets of NumberOfRvaAndSizes, relative to the optional
# header start.  Data directories begin immediately afterwards.
NRVA_OFF = {0x10B: 92, 0x20B: 108}          # PE32, PE32+
OPT_SIZE = {0x10B: 224, 0x20B: 240}         # standard optional header sizes
DIR_COUNT = 16

# A dependency is "system" if Windows itself ships it.  Anything a Mistpine
# build drags in from the toolchain (SDL, bgfx, a MinGW runtime DLL or an MSVC
# redistributable) is not: the artifact has to run on a clean machine.
#
# The Windows Universal CRT is deliberately NOT forbidden.  MinGW-w64 links the
# UCRT through the `api-ms-win-crt-*` API sets (forwarders for ucrtbase.dll),
# and both are operating-system components on Windows 10 and later -- they are
# not a redistributable the player has to install.  They are reported
# separately below so the fact stays visible in the log, and the release notes
# state the resulting floor.  The Visual C++ *runtime* (vcruntime, msvcp,
# msvcr) stays forbidden: that one does require a redistributable.
FORBIDDEN = re.compile(
    r"^(sdl\d*|bgfx|bx|bimg|fcpp|freetype|glfw|"
    r"libgcc_s_.*|libstdc\+\+-.*|libwinpthread-.*|libgomp-.*|libssp-.*|"
    r"msvcp\d+|msvcr\d+|vcruntime\d*|vcomp\d*|concrt\d*|mfc\d*)",
    re.IGNORECASE,
)

# Allowed, but called out: present because the toolchain targets the UCRT.
UCRT = re.compile(r"^(api-ms-win-crt-.*|ucrtbase.*)$", re.IGNORECASE)

# Modules that ship with the OS and are therefore always present.
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

MACHINES = {0x014C: "i386 (32-bit)", 0x8664: "x86-64", 0xAA64: "ARM64",
            0x01C0: "ARM", 0x01C4: "ARMNT", 0x0200: "IA-64"}

# The release artifact is required to be Windows x64 (PROMPT 2).
REQUIRED_MACHINE = 0x8664


class PEError(Exception):
    pass


def _u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def _u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


# --------------------------------------------------------------------------- #
# Cross-check the table above once, at import time.  This is what makes an
# off-by-N in NRVA_OFF impossible to reintroduce unnoticed: the data-directory
# array has to end exactly at the end of the standard optional header.
for _magic, _nrva in NRVA_OFF.items():
    assert _nrva + 4 + DIR_COUNT * 8 == OPT_SIZE[_magic], (
        f"PE optional-header offsets inconsistent for magic 0x{_magic:x}: "
        f"{_nrva} + 4 + {DIR_COUNT}*8 != {OPT_SIZE[_magic]}"
    )


class Section:
    __slots__ = ("name", "vsize", "vaddr", "rawsize", "rawoff")


def _sections(data: bytes, n: int, off: int) -> list[Section]:
    out = []
    for i in range(n):
        o = off + 40 * i
        if o + 40 > len(data):
            raise PEError("section table runs past the end of the file")
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
    """Yield each DLL named by an import directory table."""
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


class PEImage:
    """A PE image parsed just far enough to audit its dependencies."""

    def __init__(self, path: str):
        self.path = path
        with open(path, "rb") as fh:
            self.data = fh.read()
        data = self.data
        if len(data) < 0x40 or data[:2] != b"MZ":
            raise PEError("not a PE image (no MZ signature)")
        pe = _u32(data, 0x3C)
        if pe <= 0 or pe + 24 > len(data) or data[pe:pe + 4] != b"PE\x00\x00":
            raise PEError("not a PE image (no PE signature)")

        coff = pe + 4
        self.machine = _u16(data, coff)
        nsec = _u16(data, coff + 2)
        opt_size = _u16(data, coff + 16)
        opt = coff + 20
        if opt + opt_size > len(data) or opt_size < 96:
            raise PEError(f"implausible optional header size {opt_size}")
        self.opt_size = opt_size

        magic = _u16(data, opt)
        if magic not in NRVA_OFF:
            raise PEError(f"unknown optional header magic 0x{magic:x}")
        self.magic = magic
        self.pe32plus = magic == 0x20B

        nrva_off = opt + NRVA_OFF[magic]
        if nrva_off + 4 > opt + opt_size:
            raise PEError("NumberOfRvaAndSizes lies outside the optional header")
        nrva = _u32(data, nrva_off)
        if nrva < 1 or nrva > DIR_COUNT:
            raise PEError(
                f"implausible NumberOfRvaAndSizes {nrva} at optional-header "
                f"offset {NRVA_OFF[magic]} (expected 1..{DIR_COUNT})"
            )
        if nrva_off + 4 + nrva * 8 > opt + opt_size:
            raise PEError("data directories run past the optional header")
        self.nrva = nrva

        dir_off = nrva_off + 4
        self.dirs = {i: (_u32(data, dir_off + 8 * i), _u32(data, dir_off + 8 * i + 4))
                     for i in range(nrva)}

        if nsec == 0 or nsec > 96:
            raise PEError(f"implausible section count {nsec}")
        self.sections = _sections(data, nsec, opt + opt_size)

    @property
    def machine_name(self) -> str:
        return MACHINES.get(self.machine, f"0x{self.machine:04x}")

    def imports(self):
        return list(_import_dir(self.data, self.sections, *self.dirs.get(1, (0, 0))))

    def delay_imports(self):
        return list(_import_dir(self.data, self.sections, *self.dirs.get(13, (0, 0))))


# --------------------------------------------------------------------------- #
# Self-test: build structurally valid PE32 and PE32+ images and check the
# parser against them.  The builder writes NumberOfRvaAndSizes using the same
# NRVA_OFF table the parser reads, but the import-time assertion above pins
# that table to the documented optional-header sizes, and the corrupted-header
# cases below pin the failure modes.
def _build_fake_pe(dll_names, delay_names=(), pe32plus=True, opt_size=None,
                   nrva=None):
    magic = 0x20B if pe32plus else 0x10B
    opt_size = OPT_SIZE[magic] if opt_size is None else opt_size
    nrva = DIR_COUNT if nrva is None else nrva
    sec_va, sec_rawoff = 0x1000, 0x400

    strings = bytearray()
    offs = {}
    for n in list(dll_names) + list(delay_names):
        offs[n] = len(strings)
        strings.extend(n.encode() + b"\x00")

    def blank(names):
        out = bytearray()
        for _ in names:
            out += struct.pack("<IIIII", 0, 0, 0, 0, 0)
        out += b"\x00" * 20
        return bytearray(out)

    imp, delay = blank(dll_names), blank(delay_names)
    stride = 8
    blob_va = sec_va + 16 + len(imp) + len(delay)
    blob_va += (-blob_va) % stride
    str_rva = blob_va

    def fill(buf, names):
        for i, n in enumerate(names):
            struct.pack_into("<I", buf, 20 * i + 12, str_rva + offs[n])

    fill(imp, dll_names)
    fill(delay, delay_names)
    imp, delay = bytes(imp), bytes(delay)
    imp_va = sec_va + 16
    delay_va = imp_va + len(imp)

    body = bytearray(b"\x00" * 16) + imp + delay
    body += b"\x00" * (str_rva - (sec_va + len(body)))
    body += strings
    raw = bytes(body)
    raw += b"\x00" * ((-len(raw)) % 0x200)

    hdr = bytearray(sec_rawoff)
    hdr[0:2] = b"MZ"
    struct.pack_into("<I", hdr, 0x3C, 0x80)
    hdr[0x80:0x84] = b"PE\x00\x00"
    coff = 0x84
    struct.pack_into("<HH", hdr, coff, 0x8664 if pe32plus else 0x014C, 1)
    struct.pack_into("<H", hdr, coff + 16, opt_size)
    opt = coff + 20
    struct.pack_into("<H", hdr, opt, magic)
    struct.pack_into("<I", hdr, opt + NRVA_OFF[magic], nrva)
    d = opt + NRVA_OFF[magic] + 4
    struct.pack_into("<II", hdr, d + 8 * 1, imp_va, len(imp))
    if delay_names:
        struct.pack_into("<II", hdr, d + 8 * 13, delay_va, len(delay))
    s = opt + opt_size
    hdr[s:s + 8] = b".rdata\x00\x00"
    struct.pack_into("<IIII", hdr, s + 8, len(raw), sec_va, len(raw), sec_rawoff)
    return bytes(hdr) + raw


def self_test() -> int:
    import os
    import tempfile

    failures = 0

    def check(cond, msg):
        nonlocal failures
        if not cond:
            print(f"self-test: {msg}")
            failures += 1

    cases = [
        (["KERNEL32.dll", "msvcrt.dll", "USER32.dll"], [], False),
        (["KERNEL32.dll", "SDL3.dll"], [], True),
        (["KERNEL32.dll", "VCRUNTIME140.dll"], [], True),
        (["KERNEL32.dll", "libwinpthread-1.dll"], [], True),
        (["KERNEL32.dll"], ["bgfx.dll"], True),
        # The UCRT API sets are allowed: they are OS components, not a redistributable.
        (["KERNEL32.dll"] + [f"api-ms-win-crt-{n}-l1-1-0.dll" for n in
                             ("heap", "math", "runtime", "stdio", "string")], [], False),
        (["KERNEL32.dll", "ucrtbase.dll"], [], False),
        # ... but the Visual C++ runtime still is not.
        (["KERNEL32.dll", "MSVCP140.dll"], [], True),
        (["KERNEL32.dll", "VCRUNTIME140_1.dll"], [], True),
    ]
    with tempfile.TemporaryDirectory() as td:
        # Both layouts must parse, and produce identical verdicts.
        for pe32plus in (True, False):
            label = "PE32+" if pe32plus else "PE32"
            for i, (imp, delay, should_fail) in enumerate(cases):
                p = os.path.join(td, f"{label}-{i}.exe")
                with open(p, "wb") as fh:
                    fh.write(_build_fake_pe(imp, delay, pe32plus=pe32plus))
                try:
                    img = PEImage(p)
                except Exception as exc:  # noqa: BLE001
                    check(False, f"{label} case {i}: parser raised {exc}")
                    continue
                got_i, got_d = img.imports(), img.delay_imports()
                check(sorted(got_i) == sorted(imp),
                      f"{label} case {i}: imports {got_i} != {imp}")
                check(sorted(got_d) == sorted(delay),
                      f"{label} case {i}: delay {got_d} != {delay}")
                bad = [n for n in got_i + got_d if FORBIDDEN.match(n)]
                check(bool(bad) == should_fail,
                      f"{label} case {i}: violation {bool(bad)} != {should_fail}")

        # A corrupted/misread header must raise, never report "no imports".
        p = os.path.join(td, "badnrva.exe")
        with open(p, "wb") as fh:
            fh.write(_build_fake_pe(["KERNEL32.dll"], nrva=0))
        try:
            PEImage(p).imports()
            check(False, "nrva=0 should not be reported as 'no imports'")
        except PEError:
            pass
        p = os.path.join(td, "truncated.exe")
        with open(p, "wb") as fh:
            fh.write(_build_fake_pe(["KERNEL32.dll"], opt_size=96))
        try:
            PEImage(p).imports()
            check(False, "an optional header too small for the directories should raise")
        except PEError:
            pass
        # A non-PE file must raise, not pass.
        p = os.path.join(td, "notpe.bin")
        with open(p, "wb") as fh:
            fh.write(b"\x7fELF" + b"\x00" * 512)
        try:
            PEImage(p)
            check(False, "an ELF file should not parse as a PE")
        except PEError:
            pass

    total = 2 * len(cases) + 3
    print(f"self-test: {'FAIL' if failures else 'ok'} ({total} checks)")
    return 1 if failures else 0


def main(argv: list[str]) -> int:
    args = list(argv[1:])
    list_only = "--list" in args
    args = [a for a in args if a != "--list"]
    if args and args[0] == "--self-test":
        return self_test()
    if len(args) != 1:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    path = args[0]
    try:
        img = PEImage(path)
        imp, delay = img.imports(), img.delay_imports()
    except (PEError, OSError, struct.error) as exc:
        print(f"pe_deps: cannot read imports of {path}: {exc}", file=sys.stderr)
        # A failure to *read* the image is a failure of the audit, not a pass.
        return 1

    print(f"{path}: {img.machine_name}, "
          f"{'PE32+' if img.pe32plus else 'PE32'}, "
          f"{len(img.sections)} sections, {img.nrva} data directories")
    if not imp and not delay:
        print(f"pe_deps: {path} imports no DLLs at all (suspicious)", file=sys.stderr)
        return 1

    def tag_of(n):
        if UCRT.match(n):
            return "ucrt: OS component, Windows 10+"
        if n.lower().split(".")[0] in SYSTEM_HINT:
            return "system"
        return "REVIEW"

    for n in sorted(imp):
        print(f"  IMPORT  {n}  [{tag_of(n)}]")
    for n in sorted(delay):
        print(f"  DELAY   {n}  [{tag_of(n)}]")
    ucrt = sorted(n for n in set(imp) | set(delay) if UCRT.match(n))
    if ucrt:
        print(f"note: {len(ucrt)} Universal CRT API-set import(s); these are "
              f"forwarders for ucrtbase.dll and ship with Windows 10 and later.")

    bad = [n for n in sorted(set(imp) | set(delay)) if FORBIDDEN.match(n)]
    if bad:
        print("pe_deps: unexpected non-system runtime dependency found:", file=sys.stderr)
        for n in bad:
            print(f"  {n}", file=sys.stderr)

    if img.machine != REQUIRED_MACHINE:
        print(f"pe_deps: machine is {img.machine_name}, expected "
              f"{MACHINES[REQUIRED_MACHINE]} — this artifact is not Windows x64",
              file=sys.stderr)
        return 1

    if bad and not list_only:
        return 1
    print("pe_deps: no unexpected non-system runtime dependency")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
