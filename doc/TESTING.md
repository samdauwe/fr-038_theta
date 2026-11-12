# Testing

## The runner

```sh
./build/release/theta_tests                  # every suite, a few at a time; no arguments needed
./build/release/theta_tests --list           # the names of the suites
./build/release/theta_tests wz3_ player_     # the suites whose name contains one of the words
./build/release/theta_tests --jobs 2 --verbose
cmake --build build/release --target check   # builds and runs it
ctest --test-dir build/release -j8           # one CTest test per suite
```

[tests/test_main.c](../tests/test_main.c) is the only `main` of the test program. The suites are the functions
`int suite_<name>(int argc, char **argv)` of [tests/suites](../tests/suites); [tests/suites.def](../tests/suites.def) lists them with their
arguments (the directories of the shipped data and of the fixtures), time limit and flags. CMake reads the same file to register the CTest tests,
so adding a suite means writing the function and adding one line.

On POSIX systems the runner starts every suite in a child process:

* a crash, an `exit()` or a hang (the time limit is enforced) in one suite cannot touch another;
* the output of a suite is captured and printed only when it fails (or with `--verbose`);
* up to `--jobs` suites run at once (default: the number of processors, at most 8); suites marked `SUITE_SERIAL` measure wall-clock behaviour and
  run alone afterwards;
* `THETA_TEST_TIMEOUT_SCALE=N` multiplies the time limits (slow machines, sanitizer builds).

Elsewhere the suites run one after the other in the runner's own process.

Each suite includes the library headers with `*_IMPLEMENTATION` and `*_STATIC`, so it has a private copy of the code it tests and can use its
internals (`wz3__...`, `glr__...`). The one exception is the `linkage` suite: it compiles the implementation into one translation unit and uses
the header without it in another, which is what a user of the single-header libraries does.

## Suites

| Suite | Checks |
|---|---|
| `linkage` | the single-header layout links across translation units |
| `audio_ogg` | `ogg.h`: decoding in blocks of any size, sample-accurate seeking, damaged streams, the audio hand-over of the player |
| `wz3_theta_kx` | the 2004 document layout and the shipped document: structure fingerprint, truncation and bit flips |
| `wz3_theta_ops` | the mesh editing operators the document uses: topology, counts, positions, determinism, the parameter packing; the Text operator's cached glyph coverage at the sheet size and at lower and higher texture qualities (no fallback to the substitute font, the same letter metrics) |
| `wz3_dxt5` | the BC3 codec against the vectors of the JavaScript implementation |
| `wz3_render_plan`, `wz3_draw_list`, `wz3_shadow` | render planning, draw lists and shadow volumes against the hashes of the JavaScript implementation, plus allocation failures |
| `glr_trace` | `glr.h` against a recording GL: the call stream of the reference renderer, the shader texts, every failure path |
| `glr_egl` | `glr.h` on a real driver (headless EGL, Mesa llvmpipe); skips without EGL development files or a driver |
| `player_scene`, `player_scene_egl`, `player_replay` | the resident scene (precalculation sinks, texture plan, warm-up, the first frame uploads no bitmap), the same against a real driver with identical pictures, snapshots and the replay behind a seek |
| `player_text`, `player_credits`, `player_glcheck`, `player_args` | the remaining SDL-free player headers |
| `player_*` (the rest) | the player executable, started headless: `--licenses`, bad arguments, a missing or damaged file, fixed-time pictures, seek and replay, overlays, the GL accounting, the short soak, live playback with pause and seeks, cancelling while loading, window resizing |

## Static checks and regression data

`ctest` also runs checks that need no suite:

| Test | Checks |
|---|---|
| `headers_no_mutable_statics` | no mutable global state in the core headers |
| `no_libm_imports` | the test program imports no transcendental function from libm |
| `assets_sha256` | `assets/theta.kx` and `assets/theta.ogg` against the recorded checksums |
| `theta_inventory` | the operator classes of the document against `tests/data/theta/class_histogram.txt` |
| `theta_precalc` | every operator of the document runs, and the fingerprint over all meshes is the recorded one |
| `theta_scene_t2`, `_t14`, `_t24`, `_t210`, `_t250` | what a frame draws at those seconds of the song, against `tests/data/theta/scene_t*.txt` |
| `ogg_pcm_oracle` | `ogg.h` against the PCM of ffmpeg (only where ffmpeg is installed) |

The `theta_*` entries are regression data produced by the project itself, not oracles: when an operator is changed on purpose, regenerate the
recorded file with the tool of the same name and review the difference.

## Sanitizers and `-march=native`

```sh
cmake --preset asan && cmake --build --preset asan && ctest --preset asan -j6    # Clang, ASan + UBSan
cmake --preset native-march && cmake --build --preset native-march && ctest --preset native-march
```

The `native-march` preset enables every instruction set of the build machine, FMA included; the tests still pass because contraction is switched
off explicitly. The Mesa/EGL driver keeps one-time allocations that the leak checker reports; [tests/lsan.supp](../tests/lsan.supp) and the
runner's suppression list cover them.

## Soak

`-DTHETA_TEST_SOAK=ON` adds `player_soak_long`: the whole demo many times in deterministic steps for ten minutes of wall time, with seeks, resizes
and the player's screens in every loop. It fails on GL errors, on GL objects that differ between loops or on memory growth.

## Not covered

The WebAssembly build is built (and its size checked) by the Pages workflow, but no test runs it in a browser.
