# Exynos4 Mali-400 WebView GPU-info compatibility

Chromium 156.0.8078.47 (`1d17a2e2ee147bb7d6ae2ab6abdb3a2510c17775`)
sets `GLContextAttribs::allow_es_version_fallback = false` while collecting GPU
information. Its default requested version is ES3. Mali-400 provides ES2, so
collection fails and leaves `can_support_threaded_texture_mailbox` false.
`AwContents.ForceAuxiliaryBitmapRendering` then forces bitmap rendering even
on a hardware-accelerated Android Canvas. Normal HTML video layers are absent;
copying the same hardware-decoded frames to a CPU canvas still works.

`chromium-gles2-gpu-info.patch` is the source fix: retain the context attributes'
default ES-version fallback. This reports the actual Mali-400 / OpenGL ES 2.0
capabilities; it does not advertise ES3 or replace the decoder.

The Android tree imports a prebuilt WebView. `patch_webview_gles2.py` reproduces
that source change for the **one verified ARM library**. It removes the compiled
`strb.w r0, [sp, #0x40]` override at ELF offset `0x30416ea`, keeping the default
true written by the immediately preceding `GLContextAttribs` constructor.
The zero in r0 remains the null share-group argument to CreateGLContext.

The tool verifies the complete original and resulting library SHA-256, the
constructor/call instruction sequence, stored ZIP entry and both ZIP headers.
It preserves APK size, entry order, offsets, JNI alignment, DEX and resources;
only four instruction bytes and the two CRC fields change. It refuses another
WebView revision or an already-patched input. Updating WebView therefore requires
reviewing this compatibility step or rebuilding with the source patch.

The generated APK **must be signed again**: its old APK signatures are no longer
valid. The shared `exynos4_webview_gles2` BUILD_PREBUILT does this with the normal
system development certificate. It consumes the same ARM prebuilt as the normal `webview` module
and overrides its installation only in products that select the replacement.
`smdk4412-common/common.mk` selects it for Exynos4 products inheriting that tree.
Products outside this tree and other architectures keep the original module.
Runtime verification is currently limited to GT-N8000.

Verification on GT-N8000: before the change GL information was empty, threaded
texture mailboxes false, normal video blank while canvas copies showed images.
With the change Jelly reports ARM / Mali-400 MP / OpenGL ES 2.0, mailboxes true,
and displays the same OMX.SEC.AVC.Decoder video normally without WebView flags.

Run the host integration check from the Android root:

```
python3 device/samsung/smdk4412-common/webview-compat/test_patch_webview_gles2.py \
    external/chromium-webview/prebuilt/arm/webview.apk
```

The test checks valid output CRCs, unchanged entries and offsets, the exact
12-byte APK change boundary, and rejection of unknown/already-patched input.

## WebView updates

This transformation runs during the ROM build. It does not intercept or modify
WebView APKs installed later by an app store, GApps package, or manually. If such
an update becomes the selected provider, the GLES2 correction must be present in
that provider as well; otherwise the original rendering failure may return.
The current ROM selects `com.android.webview`; installing a differently named or
signed Google provider is not itself an update of that system package.

For a new bundled prebuilt, review the source change against its Chromium
revision and verify the compiled instruction and complete library hashes again.
Do not bypass the version guard. A WebView source build carrying the source patch
is the preferred long-term way to maintain the change across updates.
