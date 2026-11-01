#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Apply the documented GLES2 GPU-info fallback to one verified ARM WebView.

Preserve ZIP layout (including JNI alignment), update the affected entry's CRC,
and leave signing to BUILD_PREBUILT. Unknown WebView revisions fail closed.
"""
import hashlib
import io
import struct
import sys
import zipfile
import zlib
from pathlib import Path

LIBRARY = "lib/armeabi-v7a/libwebviewchromium.so"
ORIGINAL_SHA256 = "80068f4d582e29439f7fb72eecc2d3d71c8dc7238b301f8b7f970b155f1553e4"
PATCHED_SHA256 = "68e101b14dff3ea8ed15eecae32ac7e12f780dcfda4e1b1e497ed8a5d01a62c0"
OFFSET = 0x30416EA
BEFORE = bytes.fromhex("8df84000")  # strb.w r0, [sp, #0x40]
AFTER = bytes.fromhex("00bf00bf")   # nop; nop -- keep constructor's default true
CONTEXT_OFFSET = 0x30416E0
CONTEXT = bytes.fromhex("01f0f5f90020394632468df84000")


def patch_apk(source: Path, destination: Path) -> None:
    raw = bytearray(source.read_bytes())
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        entry = archive.getinfo(LIBRARY)
        original = archive.read(entry)
        if hashlib.sha256(original).hexdigest() != ORIGINAL_SHA256:
            raise ValueError("Unknown ARM WebView library: rebuild with the accompanying source patch or review a new verified patch; refusing to guess offsets")
        if original[CONTEXT_OFFSET:CONTEXT_OFFSET + len(CONTEXT)] != CONTEXT:
            raise ValueError("GPU-info context constructor/call sequence changed")
        if entry.compress_type != zipfile.ZIP_STORED or entry.flag_bits & 8:
            raise ValueError("Expected stored JNI library without a data descriptor")
        central_start = archive.start_dir
    changed = bytearray(original)
    if changed[OFFSET:OFFSET + len(BEFORE)] != BEFORE:
        raise ValueError("GPU-info fallback override does not match")
    changed[OFFSET:OFFSET + len(AFTER)] = AFTER
    if hashlib.sha256(changed).hexdigest() != PATCHED_SHA256:
        raise ValueError("Unexpected patched library hash")
    local = entry.header_offset
    header = struct.unpack_from("<4s5H3I2H", raw, local)
    if header[0] != b"PK\x03\x04" or header[6] != entry.CRC:
        raise ValueError("Invalid local ZIP header")
    data_start = local + 30 + header[-2] + header[-1]
    if raw[data_start:data_start + len(original)] != original:
        raise ValueError("JNI library ZIP offset mismatch")
    crc = zlib.crc32(changed)
    raw[data_start + OFFSET:data_start + OFFSET + len(AFTER)] = AFTER
    struct.pack_into("<I", raw, local + 14, crc)
    cursor = central_start
    found = 0
    while raw[cursor:cursor + 4] == b"PK\x01\x02":
        values = struct.unpack_from("<4s6H3I5H2I", raw, cursor)
        name_len, extra_len, comment_len = values[10:13]
        name = raw[cursor + 46:cursor + 46 + name_len].decode("utf-8")
        if name == LIBRARY:
            if values[7] != entry.CRC or values[-1] != local:
                raise ValueError("Central ZIP header mismatch")
            struct.pack_into("<I", raw, cursor + 16, crc)
            found += 1
        cursor += 46 + name_len + extra_len + comment_len
    if found != 1:
        raise ValueError("Expected exactly one JNI library entry")
    with zipfile.ZipFile(io.BytesIO(raw)) as archive:
        if hashlib.sha256(archive.read(LIBRARY)).hexdigest() != PATCHED_SHA256:
            raise ValueError("Output ZIP CRC/library verification failed")
    destination.write_bytes(raw)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: patch_webview_gles2.py INPUT.apk OUTPUT.apk (output must be signed)")
    try:
        patch_apk(Path(sys.argv[1]), Path(sys.argv[2]))
    except (ValueError, KeyError, zipfile.BadZipFile) as error:
        raise SystemExit(str(error))
