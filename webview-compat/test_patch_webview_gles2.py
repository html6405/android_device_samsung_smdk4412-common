#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Integration checks against the real ARM prebuilt; run with its APK path."""
import hashlib
import sys
import tempfile
import zipfile
from pathlib import Path
import patch_webview_gles2 as patcher


def main(apk):
    with tempfile.TemporaryDirectory(prefix="exynos4-webview-test-") as tmp:
        output = Path(tmp) / "patched.apk"
        patcher.patch_apk(apk, output)
        before, after = apk.read_bytes(), output.read_bytes()
        assert len(before) == len(after)
        assert sum(a != b for a, b in zip(before, after)) == 12
        with zipfile.ZipFile(apk) as original, zipfile.ZipFile(output) as patched:
            assert patched.testzip() is None
            assert original.namelist() == patched.namelist()
            for info in original.infolist():
                other = patched.getinfo(info.filename)
                assert info.header_offset == other.header_offset
                assert info.compress_type == other.compress_type
                if info.filename != patcher.LIBRARY:
                    assert original.read(info) == patched.read(other)
                else:
                    assert hashlib.sha256(patched.read(other)).hexdigest() == patcher.PATCHED_SHA256
        invalid = Path(tmp) / "unknown.apk"
        with zipfile.ZipFile(invalid, "w") as archive:
            archive.writestr(patcher.LIBRARY, b"unverified WebView revision")
        for rejected in (invalid, output):
            destination = Path(tmp) / "must-not-exist.apk"
            try:
                patcher.patch_apk(rejected, destination)
            except ValueError:
                pass
            else:
                raise AssertionError("unverified/already-patched APK accepted")
            assert not destination.exists()
    print("PASS: exact change boundary, ZIP CRC, unchanged entries/offsets, version guards")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: test_patch_webview_gles2.py path/to/original-arm-webview.apk")
    main(Path(sys.argv[1]))
