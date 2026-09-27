# Verification record — 2026-09-17

Host: Apple M4 Pro, macOS, AppleClang 21 / Xcode beta SDKs.
These are local results, not a claim of every-platform compatibility.

| Configuration | Result |
| --- | --- |
| Multi-backend desktop (OpenGL+Metal+Vulkan in one lib) | Build and 38/38 CTest tests passed; auto-selected Metal |
| Single-backend `MEOWY_BACKEND=OpenGL` | Compiles OpenGL only; 33/33 tests passed; auto-selected OpenGL |
| `MEOWY_ENABLE_VULKAN=OFF` / `MEOWY_ENABLE_METAL=OFF` | Configure+build; only the enabled backends compiled; auto picks the top enabled one |
| AddressSanitizer + UndefinedBehaviorSanitizer | previously 24/24 suites passed; leak detection unavailable on this host |
| Fresh external CMake consumer | `add_subdirectory` + target link built and ran |
| Native visionOS simulator | Metal-only build (no GLFW/SDL/Vulkan/OpenGL linked); launches + auto-selects Metal |
| Native visionOS device target | Static library and unsigned app built; not run on hardware |
| Dear ImGui (`MEOWY_WITH_IMGUI=ON`) desktop | Build + 38/38 CTest passed; pinned red ImGui window RAN and rendered on OpenGL, Metal, AND Vulkan/MoltenVK (identical bbox) |
| Dear ImGui on visionOS simulator | Compiles + links (imgui_impl_metal, no GLFW) and app launches without crash; pinned-window pixel readback NOT run here (see note) |
| CoreAudio device smoke check | Muted sound creation/play/stop, source-copy and volume calls passed |

## Runtime backend selection

Startup-time backend selection was verified on this Apple-silicon Mac from a
single multi-backend binary (`backend_selection_*` ctests + manual runs):

- **Automatic -> Metal.** `InitWindow` with no preference selects Metal.
- **Explicit OpenGL** (`SetPreferredBackend(Backend::OpenGL)`) initializes OpenGL.
- **Explicit Vulkan/MoltenVK** initializes `Vulkan (MoltenVK)`; the Vulkan-only
  tests (`vulkan_shader_checks`, `vulkan_shadow_regression`) run real Vulkan
  rendering in the multi-backend build by preferring Vulkan.
- **Simulated init failure -> clean fallback.** `MEOWY_FORCE_BACKEND_FAIL=metal`
  makes Metal's init fail; selection falls back to Vulkan, tearing down the
  failed attempt's window first (each attempt is logged).
- **All backends fail** (`MEOWY_FORCE_BACKEND_FAIL=metal,vulkan,opengl`):
  `InitWindow` throws without crashing and leaves no window/backend behind.
- **`SetPreferredBackend` after init** is rejected and logged (no live switch).
- String parsing, compiled-vs-available queries, and name reporting are covered
  by `backend_selection_logic`.

Honest scope of what RAN vs was only COMPILED / logic-only:
- RAN on this host: Metal (automatic + the general suite), OpenGL (explicit +
  full single-backend suite), Vulkan/MoltenVK (explicit + the Vulkan tests),
  the simulated-failure fallback to Vulkan, and visionOS Metal auto-selection.
- NOT run here (code/logic only): the Windows/Linux automatic order
  (Vulkan -> OpenGL) is compile-time platform logic exercised only by reading;
  the "Vulkan dependencies missing -> auto-disable Vulkan" CMake path was not
  exercised because Vulkan/glslang are installed on this machine (the code
  disables it and prints a status message; the enable-option path with Vulkan
  turned off WAS verified via `MEOWY_ENABLE_VULKAN=OFF`).

## What the tests cover

- App lifecycle, requested close, cleanup after a thrown desktop frame.
- Render textures, readback, screenshots, depth, ordering across texture
  updates, repeated clears and window reinitialization.
- Custom shaders, named scalar/vector/int/matrix values, float/vector arrays
  and per-draw uniform snapshots, including vertex-stage Metal uniforms.
- Actual MSAA edge coverage, alpha/additive/multiply blending and scissor clips.
- GPU instancing (unlit and lit), persistent GPU mesh upload + draw, glTF
  skeletal rendering, parent transforms, LINEAR/STEP/CUBICSPLINE sampling,
  quaternion rotation, scale and active-scene filtering.
- Direct-light metallic/roughness shading, normal/light direction and emission.
  Texture-map shader paths are implemented; not every map combination has a
  dedicated visual regression fixture.
- glTF feature parsing (gltf_checks): alpha mode/cutoff, `KHR_texture_transform`
  offset/scale/rotation, `TEXCOORD_1` secondary UV storage, and clamping of
  more than four joint influences to the four strongest (renormalized).
- Image-based lighting: a metallic surface under a blue environment cubemap
  reflects blue. Verified natively on OpenGL, Metal, AND Vulkan (Vulkan gained a
  `samplerCube` binding + `shadePBRIBL` branch in batch.frag; the earlier
  flat-ambient fallback is gone; readback rgba=0,112,181 on all three).
- Precomputed split-sum IBL: `GenEnvironmentLightMaps` builds, on the CPU
  (backend-agnostic, deterministic), a cosine-convolved diffuse irradiance
  cubemap, a GGX roughness-prefiltered specular cubemap (single CPU-convolved
  base level + hardware `GenTextureMipmaps` mip chain, roughness selects LOD),
  and a BRDF integration LUT (Karis split-sum, RG in RGBA8). Verified by
  readback in `ibl_precompute_checks` on OpenGL AND Metal: irradiance face
  rgba=0,115,228, prefilter face rgba=0,121,241, BRDF LUT at high NdotV/low
  roughness rg=255,0 (scale~=1,bias~=0), and a metallic quad rendered through
  the `shadePBRSplitSum` shader path reads back rgba=0,96,163 (blue). On Vulkan
  the maps are generated and validated by readback identically, but the Vulkan
  lit shader still consumes the runtime cubemap-sample path (not split-sum), so
  its rendered surface (rgba=0,112,181) comes from the runtime IBL branch;
  `SetEnvironmentLightPrecomputed` also sets the source cubemap so Vulkan
  degrades to the runtime approximation rather than losing IBL. The prefilter is
  honestly a single-convolved-level approximation plus hardware mips, not a full
  per-mip GGX convolution.
- Directional shadow mapping: a floor region under an occluder is darker than an
  unshadowed region. Verified on OpenGL (under=101 lit=189), Metal (under=101
  lit=189) AND Vulkan (under=101 lit=189) via material_checks. Metal gained a
  depth-only `vs_shadow` pipeline + `depth2d` sampler + PCF in fs_main.
  Vulkan shadow mapping (shadow.vert/shadow.frag depth pass writing an R32F map,
  PCF in batch.frag) was long reported failing under MoltenVK. An isolated
  rendering regression (`vulkan_shadow_regression`, Vulkan-only) drives the
  shadow depth pass directly and reads the R32F map back to the host via an
  internal `ReadShadowMap()` hook: it traced the failure to a real bug in our own
  shadow pipeline -- the input-assembly `topology` was left zero-initialized
  (VK_PRIMITIVE_TOPOLOGY_POINT_LIST), so occluders rasterized as vertex points
  and the map read back near-empty (4 vertex texels). Setting TRIANGLE_LIST fixed
  it; the readback now shows the full projected occluder (1024/4096 texels,
  center depth ~0.39), so `SupportsShadows()` returns true on Vulkan and the
  end-to-end material_checks shadow assertion passes. This was NOT a MoltenVK
  defect -- the earlier "occluder fragments not stored" note was an incorrect
  diagnosis, now corrected.
- Persistent GPU mesh cache: verified on OpenGL, Metal, AND Vulkan (Vulkan gained
  device-resident vertex buffers via UploadMeshBuffer/DrawMeshBuffer;
  backend_checks "uploaded mesh renders from persistent buffer" passes on all
  three instead of Vulkan silently streaming).
- glTF morph targets (blend shapes): loaded (no longer rejected), default weights
  applied, and `SetMeshMorphWeights` blends position/normal deltas on the CPU.
  Verified on all three backends via gltf_checks: a triangle apex rises only when
  the morph weight is set (upper-region lit pixels 0 -> 135). Morph-weight
  *animation channels* are skipped (skeleton still animates); morphs are driven
  programmatically via SetMeshMorphWeights.
- CompressData/DecompressData now emit/consume RAW DEFLATE (raylib's on-wire
  format), verified: the output's first byte is not the 0x78 zlib header and the
  pair round-trips a 4KB payload losslessly.
- ImageDither implements Floyd-Steinberg error diffusion to per-channel bit
  depths, verified: quantizing a gradient to 2 bits/channel reduces distinct
  levels to <= 8.
- Transparent (BLEND) rendering: independent depth-write (a near transparent
  quad no longer depth-occludes a farther one) plus back-to-front sorting.
  Verified on OpenGL/Metal/Vulkan via transparency_checks: the composite of two
  overlapping transparent quads is identical regardless of submission order
  (center = 128,0,64 red-over-blue in both orders). Added a backend SetDepthMask
  (glDepthMask / a test-no-write Metal depth state / a third Vulkan pipeline
  variant). Documented limit: the sort is per-draw center-distance, not
  per-triangle, so self-overlapping concave transparent meshes still rely on
  submission order.
- glTF morph-weight ANIMATION channels: ModelAnimation now carries per-frame
  weight tracks; UpdateModelAnimation drives the mesh morph weights. Verified on
  all three backends via gltf_checks: a weight track animates 0 -> 1 across the
  clip (frame0 weight 0.00, last frame 1.00).
- Sanitizer sweep (ASan + UBSan, OpenGL): the CPU utility, image, model, shape,
  gltf/morph, transparency, material, backend, render, and animation suites all
  run clean (exit 0, zero sanitizer reports). UBSan caught and I fixed a signed-
  integer-overflow in the new Perlin/cellular noise hash (now unsigned).
- Cascaded directional shadow maps: the view frustum is split into up to 4
  depth ranges, each with its own light-space shadow map (higher resolution near
  the camera). Verified natively on OpenGL AND Metal via cascaded_shadow_checks:
  occluders darken the floor (846 shadowed pixels on GL, 752 on Metal with 3
  cascades vs 680/534 with 1), confirming both real shadowing and that more
  cascades cover at least as much area. `IsCascadedShadowSupported()` reports
  support (OpenGL + Metal; false on Vulkan, where the test SKIPs).
- Showcase: a 6th scene demonstrates cascaded shadows + IBL + a pulsing morph
  mesh + depth-sorted transparent panels together; it renders headless (frame-
  limited screenshot) on OpenGL, Metal, and Vulkan. All six scenes exit 0 on
  Vulkan; single directional shadows are now active there (cascaded shadows
  still fall back gracefully on Vulkan).
- Native visionOS feature test: the visionOS sample was extended to exercise
  shadows, IBL, morph targets, and transparency, and RUN in the booted
  simulator (not just compiled). MEOWY_SMOKE_TEST launches it via simctl,
  renders 12 frames, and asserts on a 1800x1200 readback: dark background clear
  and a lit box under IBL + directional light. Result: 3 passed, 0 failed.
- Dear ImGui integration (`MEOWY_WITH_IMGUI=ON`): the public seam is
  `IsImGuiAvailable()`/`InitImGui()`/`ShutdownImGui()`; the frame is driven by
  the core (`BeginDrawing` -> ImGui NewFrame, `EndDrawing` -> ImGui render before
  present). No ImGui/graphics types appear in public headers.
  - RAN on this Apple-silicon Mac: the `imgui_demo` sample renders a window
    pinned at screen (10,10) sized 260x150 with a solid-red background, captured
    headless via a mid-frame `TakeScreenshot` (deferred to `EndDrawing` so the
    still-active drawable/swapchain image is readable). A standalone PNG checker
    (`tests/tools/png_bbox.c`) found the red window at bbox x[11..268] y[11..158]
    on OpenGL, Metal, AND Vulkan/MoltenVK -- identical on all three, proving the
    widgets composite over the scene and are captured. The full 38/38 CTest
    suite also passed with ImGui enabled, and the default ImGui-OFF build still
    compiles (`IsImGuiAvailable()` == false).
  - visionOS ("by Metaling the way"): renders through `imgui_impl_metal` with a
    custom GLFW-free platform layer that feeds ImGui IO (display size/scale from
    the UIKit view, pointer from engine touch input). The library and
    `visionos_showcase` app COMPILE + LINK for the simulator (no GLFW compiled)
    and the app LAUNCHES without crashing (scene reaches foreground-active, Metal
    initializes). The pinned-window PIXEL readback was NOT obtained in this
    headless session because the visionOS simulator's CADisplayLink does not
    advance frames without a visible volumetric Simulator window (unavailable
    here). The Metal render call used on visionOS
    (`ImGui_ImplMetal_RenderDrawData` into the current drawable encoder) is the
    same code path pixel-proven on desktop Metal above.
- raylib 6.0 API surface: all 600 manifest functions are declared AND defined
  (proven by the `raylib6_conformance` link target). Beyond linkage, an
  anti-stub gate in `tests/compat/conformance_check.py` locates each function's
  DEFINITION and classifies its body: 597 are REAL implementations, 3 are
  documented trivial/unsupported no-ops (see below), 0 are undefined or
  undocumented stubs. The gate fails CI if any function is an empty/trivial
  constant-return body not explicitly listed as `platform_unavailable` or
  `intentional_noop` in `parity_overrides.json`. "Declared + defined + non-stub"
  is still NOT "behaviorally identical to raylib"; the test-`verified` entries
  carry behavioral evidence (see `docs/RAYLIB6_PARITY.md`, regenerated by
  `tests/compat/parity_report.py`).
- Explicitly unsupported / trivial-by-design (the only 3 non-REAL functions):
  `SetGamepadVibration` is a documented no-op because GLFW (through 3.4) exposes
  no gamepad rumble/haptics API on the desktop backend (glfw/glfw#1042);
  `UnloadVrStereoConfig` is a no-op because MeowyRender's `VrStereoConfig` owns no
  GPU resources; `UpdateMusicStream` is a no-op because miniaudio services music
  streaming on its own audio thread (playback continues without a manual refill).
- Audio DSP processors now REAL and behaviorally tested (`audio_processor_checks`):
  `AttachAudioMixedProcessor`/`DetachAudioMixedProcessor` tap the final mix on the
  audio thread (the engine runs in miniaudio no-device mode, pulled through our
  own `ma_device` callback); `AttachAudioStreamProcessor`/`DetachAudioStreamProcessor`
  transform a stream's frames in place; `SetAudioStreamCallback` generates frames.
  Verified: a stream processor doubles pushed samples (0.25 -> 0.5), a fill
  callback writes 0.5 then a processor doubles to 1.0, and a mixed processor is
  invoked during playback and stops after detach.
- `GetClipboardImage` reads the real system pasteboard (NSPasteboard on macOS /
  UIPasteboard on iOS/visionOS, via `clipboard_apple.mm`) and returns RGBA8
  pixels; `rcore_util_checks` reads it back (464x114 image observed on the dev
  host). GLFW itself has no image clipboard, so on non-Apple desktops it honestly
  returns an empty image. `ImageDither` is a full Floyd-Steinberg error-diffusion
  implementation (an earlier note calling it a no-op placeholder was inaccurate).
  Deterministic CPU suites added this pass: `rcore_util_checks` (base64, CRC32/
  MD5/SHA1/SHA256 known-answer vectors, compression round-trip, random-sequence
  uniqueness, camera math, text/file utilities, automation events, Wave utils),
  `shapes_checks` (collision helpers + spline evaluators), `textures_util_checks`
  (color/pixel/image utilities + transforms), `models_util_checks` (mesh
  generators + ray/triangle/quad/mesh collision).
- `DrawTextPro` rotation: a 90-degree rotated run forms a vertical column
  (taller than wide) anchored at its origin, verified by pixel-bounds scan on
  all three backends.
- Native cubemap faces, skyboxes, mipmaps and side-by-side stereo viewports.
- All 13 uncompressed CPU/GPU pixel formats, packed formats, float32/float16,
  HDR load/export/resize, all 65,536 half-float bit patterns, malformed inputs.
- Supported compressed GPU formats and decoded readback. Driver-unsupported
  formats print `SKIP`; that does not count as support verification.
- A Vulkan frame with 2,200 separate draws crossing transient-resource recycling.
- OBJ index validation, truncated/invalid UTF-8, WAV export, and WAV/MP3/FLAC/Ogg
  file and memory decoding.
- The native visionOS smoke app checks a lit cube, text, a high-DPI scissor
  rectangle and a background pixel in its 1800×1200 screenshot.

The five-scene desktop showcase was run on all three backends. Captures of its
cubemap/animation/instancing scene were visually inspected for consistency.

## Reproduce

```sh
cmake --build build
ctest --test-dir build --output-on-failure
# Repeat with build-metal and build-vk.

ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir build-sanitize \
  -R 'image_checks|audio_checks|audio_codec_checks|asset_checks|animation_checks' \
  --output-on-failure
```

The sanitizer build uses `-fsanitize=address,undefined -fno-omit-frame-pointer`
and matching executable linker flags. Desktop graphics tests need an active
graphical session. Hardware audio initialization is separate from normal CTest:
`build/samples/audio_checks device`; it was tested with sound volume set to zero.

For the native simulator app, install the bundle using your simulator UUID and
launch with `SIMCTL_CHILD_MEOWY_SMOKE_TEST=1` and
`SIMCTL_CHILD_MTL_DEBUG_LAYER=1`. It checks its screenshot and exits itself.

## Explicit limits and unresolved checks

- Windows and Linux builds/runtime have not been tested here.
- No physical Vision Pro or physical controller/input verification.
- visionOS hosting is windowed UIKit, not immersive CompositorServices or
  tracked-headset rendering. The VR API supplies untracked stereo simulation.
- Full GPU shader instrumentation on the visionOS simulator rejects pipeline
  creation with a "buffer binding has argument index N greater than 30" error.
  Root cause (investigated 2026-09-17): the visionOS 27 simulator GPU caps
  render pipeline buffer bindings at index 30, and Metal's GPU-validation
  instrumentation reserves an index above the shader's highest binding for its
  bounds-checking metadata. A minimal probe pipeline using only buffer(0) +
  buffer(1) fails identically with "argument index 31 > 30", so the failure is
  an Xcode-beta simulator instrumentation limitation, not a MeowyRender shader
  defect. As a portability improvement the built-in Metal pipeline no longer
  uses an `MTLVertexDescriptor` (`vs_main` pulls vertices via `[[vertex_id]]`),
  lowering buffer pressure. API-validation native rendering passes and the
  1800x1200 screenshot smoke test passes. Full GPU-validation on the simulator
  is expected to reject any pipeline until Apple raises the simulator limit or
  changes instrumentation; on-device (xros) validation is unaffected.
- An earlier CoreAudio initialization hang did not recur in the final muted
  device test; other device configurations remain unverified.
- Vulkan submission is serialized. This is correctness coverage, not a
  performance benchmark or production-readiness certification.
- Full raylib 6.0 function coverage: every function in the extracted manifest is
  declared, defined, and non-stub (0 missing; 597 REAL + 3 documented no-ops; the
  anti-stub conformance gate enforces this). "Declared + defined + non-stub" is
  NOT "behaviorally identical to raylib"; the test-`verified` entries carry test
  evidence, the remaining are `implemented-unverified` (present and non-stub, but
  without a dedicated regression fixture). Documented behavioral differences:
  `CompressData`/`DecompressData` use raw DEFLATE (matching raylib's on-wire
  format). The only functions that are intentionally not full implementations are
  the three no-ops listed above (`SetGamepadVibration`, `UnloadVrStereoConfig`,
  `UpdateMusicStream`); `GetClipboardImage`, the audio stream/mixed processors,
  and `ImageDither` are now REAL (previously mis-described here as stubs/no-ops).
- Advanced-feature scoping (see the README for details): persistent GPU mesh
  uploads (OpenGL/Metal/Vulkan — now on all three), lit GPU instancing (all
  backends), glTF alpha mask/cutoff (all backends), `KHR_texture_transform` +
  secondary UV set (CPU-applied to the sampled albedo coordinate), >4-joint
  clamping, image-based lighting (OpenGL/Metal/Vulkan — native on all three),
  single directional shadow mapping (OpenGL + Metal + Vulkan) and cascaded
  directional shadow mapping (OpenGL + Metal), glTF morph
  targets + morph-weight animation (all backends, CPU-blended), and transparent
  BLEND rendering with independent depth-write + back-to-front per-draw sorting
  (all backends), and precomputed split-sum IBL (irradiance + prefiltered
  specular + BRDF LUT) consumed by the lit shader on OpenGL and Metal (generated
  and readback-validated on Vulkan, but rendered via the runtime path there).
  Still outside the implemented path: full per-mip GGX prefilter convolution
  (the prefilter is a single-convolved-level + hardware-mip approximation),
  contact-hardening shadows, and cascaded (multi-split) directional shadow
  mapping on Vulkan (single-split Vulkan shadows now work; cascaded stays
  OpenGL + Metal). The previously reported native-Vulkan single-shadow failure
  was root-caused (uninitialized shadow-pipeline topology) and fixed; see the
  shadow-mapping entry above. Transparent sorting is per-draw, not per-triangle.
- Backend feature coverage is still intentionally uneven: single directional
  shadow mapping is OpenGL + Metal + Vulkan; cascaded shadow mapping is
  OpenGL + Metal only. `IsShadowMappingSupported()` / `IsCascadedShadowSupported()`
  report availability per backend. These are documented divergences, not silent
  regressions.

The MeowyEngine checkout was not integrated or rewritten during this pass.
