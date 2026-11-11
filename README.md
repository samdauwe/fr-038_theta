# theta (native)

A native player for **fr-038: theta**, the demo by Farbrausch (The Ultimate Meeting 2004, 800x600), written in C99 on top of SDL3 and
OpenGL ES 3, for Linux, Windows, macOS and the browser (WebAssembly, WebGL 2). It evaluates the original compact werkkzeug3 production, generates
its textures and geometry, executes its timeline and plays its soundtrack, with results that are bit-identical to the original arithmetic where
that can be checked.

Original credits: **paniq** (concept, music, graphics, design, direction), **cp** (additional graphics, design), **ryg** (main code),
**chaos** (code), **fiver2, giZMo** (werkkzeug3 co-development), **kb, yoda**.

## Build and run

Requirements: a C99 compiler (GCC or Clang), CMake 3.25 or newer, Ninja (or another generator), and either an installed SDL3
(3.2 or newer) or a network connection: without an installed SDL3 the pinned release is fetched and built as a static library.
Linux needs the usual SDL3 build dependencies (X11 and/or Wayland, ALSA, Mesa/EGL development files).

```sh
cmake --preset release            # build/release
cmake --build --preset release
./build/release/theta             # plays the demo; Esc quits
```

The executable looks for `theta.kx` and `theta.ogg` in `assets/` next to itself (the build copies them there). `--kx PATH` and `--ogg PATH`
play other files. The shape of theta is 4:3 and its own black bars are cropped, so the window shows the 5:3 between them; `--aspect W:H`
overrides the shape and shows everything.

| Key | Action |
|---|---|
| Space | pause |
| Left / Right | seek 10 s back / forward |
| F | fullscreen |
| C | credits |
| F10 / F11 | statistics overlay / statistics on stdout |
| Esc | quit |

Useful options: `--size 1280x720`, `--fullscreen`, `--no-vsync`, `--no-audio`, `--single-thread`, `--texture-quality high|normal|low`
(`high` is the default, the authored size; every step down halves the width and height of the generated textures), `--licenses`. The options for
tests and measurements (`--time`, `--screenshot`, `--self-check`, `--gl-check`, `--soak`, ...) are described at the top of
[src/player/main.c](src/player/main.c).

## WebAssembly

The same player runs in the browser (WebGL 2, Web Audio) as one page, `index.html`, plus `index.js`, `index.wasm` (about 1.5 MB) and `index.data`
(about 5 MB: the document and the soundtrack, downloaded before the player starts; the page shows how far it is). Requirements:
[Emscripten](https://emscripten.org/docs/getting_started/downloads.html) 7.0.0 or newer, CMake 3.25+ and Ninja. SDL3 is fetched as for the
native build.

```sh
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install 7.0.0 && ~/emsdk/emsdk activate 7.0.0
source ~/emsdk/emsdk_env.sh                 # sets EMSDK and puts emcc on PATH

cmake --preset web                          # build/web
cmake --build --preset web
python3 -m http.server -d build/web/dist    # then open http://localhost:8000
```

The page asks for a click (browsers only start audio after one) and for a texture quality: *Normal* is the default there,
`?quality=low|normal|high` in the address preselects it. The loader bar shows how far loading is. The keys are the native ones. A browser without
WebGL 2 gets a message on the page. `?args=--stats` (any player options) prints the player's statistics to the console.

Browsers have no worker thread for a static page (it needs COOP/COEP headers), so the player loads in 12 ms slices between two pictures of the
loader (`--single-thread`, which also works natively and is tested). Use the Emscripten that comes with the SDK: a Binaryen from another source
can reject the flags of a newer Emscripten (`Unknown option '--enable-bulk-memory-opt'`).

### GitHub Pages

[.github/workflows/pages.yml](.github/workflows/pages.yml) builds the web player with a pinned Emscripten on every push and pull request, and
deploys it when the push is to `main`. Once per repository: *Settings > Pages > Build and deployment > Source: GitHub Actions*. The site is then
at `https://<user>.github.io/<repository>/`. The job fails when `index.wasm` outgrows 4 MiB or `index.data` 5.5 MiB.

## Tests

```sh
cmake --build --preset release --target check     # or: ./build/release/theta_tests
ctest --preset release -j8                        # the same suites, one CTest test each
```

`theta_tests` is a single program that needs no arguments. It runs every suite of [tests/suites](tests/suites) in a process of its own, a few at
a time, and prints one line per suite; the output of a suite is shown when it fails. The player itself is started headless (SDL offscreen video
driver, dummy audio) by a few of the suites and they skip when the machine has no OpenGL ES context. [doc/TESTING.md](doc/TESTING.md) describes
the suites, the static checks and the sanitizer and `-march=native` presets.

## Style

The code is C99, formatted by [.clang-format](.clang-format): `cmake --build build/release --target format` applies it and
`--target format-check` verifies it.

## Layout

| Path | Contents |
|---|---|
| [include/theta/core](include/theta/core) | `v2.h` (V2 synthesizer and sequencer) and `wz3.h` (the werkkzeug3 layers: math, KX document, bitmaps, meshes, runtime, frame jobs); no platform code |
| [include/theta/audio](include/theta/audio) | `ogg.h`: the Ogg Vorbis soundtrack |
| [include/theta/render](include/theta/render) | `glr.h`: the OpenGL ES 3 / GL 3.3 executor of the frame jobs; no window, no loader |
| [include/theta/player](include/theta/player) | `player_*.h`: the SDL-free parts of the player (clock and audio hand-over, loading, resident scene, picture decoder, seeking, text, credits, command line) |
| [src/player](src/player) | `main.c`: the SDL3 application (native and Emscripten) |
| [web](web) | `index.html`: the page of the WebAssembly build |
| [tools](tools) | `theta_inventory` (the classes of a KX document), `theta_precalc` (runs every operator of a document and reports what does not work), `theta_scene` (what a frame draws) |
| [tests](tests) | the test runner, its suites, the reference fixtures (`tests/data`) and the Ogg oracle |
| [cmake](cmake) | the build modules and scripts used by `CMakeLists.txt` |
| [assets](assets), [third_party](third_party) | the production data, the licences embedded in the player, stb_vorbis and stb_image |
| [doc](doc) | architecture, testing and the reference implementation |

The libraries are single headers in the style of stb: define `V2_IMPLEMENTATION`, `WZ3_IMPLEMENTATION` or `GLR_IMPLEMENTATION` in exactly one
source file before including the header. [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md) explains the design.

## Build options

| Option | Default | Meaning |
|---|---|---|
| `THETA_BUILD_PLAYER` | ON | build the SDL3 player |
| `THETA_BUILD_TOOLS` | ON (top level, not Emscripten) | build `theta_inventory`, `theta_precalc`, `theta_scene` |
| `THETA_BUILD_TESTS` | ON (top level, not Emscripten) | build `theta_tests` and register it with CTest |
| `THETA_FETCH_SDL3` | ON | fetch SDL3 (`THETA_SDL3_TAG`) when no installed one is found |
| `THETA_WEB_MAX_MEMORY` | 2GB | Emscripten only: the largest the WebAssembly memory may grow to |
| `THETA_SANITIZE` | OFF | AddressSanitizer and UndefinedBehaviorSanitizer |
| `THETA_V2_RONAN` | ON | include the Ronan speech synthesizer (V2 channel 15) |
| `THETA_TEST_SOAK` | OFF | add the ten-minute soak test |

## Determinism

The generated pictures and the audio must match the original arithmetic, so the code relies on strict IEEE-754 binary32 arithmetic: no
`-ffast-math`, no fused multiply-add contraction (`-ffp-contract=off`, set through `theta::headers`), and the transcendental functions are
implemented in the headers instead of taken from the C library. A test checks that the player imports none of them from libm, and the
`native-march` preset builds with `-march=native` to prove that contraction stays off.

## Licences

The code in this repository is derived from the released werkkzeug3 sources (BSD), v2redux (public domain), the SGI GLU tessellator and the Arimo
and Gelasio fonts (SIL OFL); it uses stb_vorbis and stb_image (public domain / MIT). The production data is CC BY-SA 3.0. The notices are in
[third_party/licenses](third_party/licenses) and [assets/ATTRIBUTION.md](assets/ATTRIBUTION.md), and `theta --licenses` prints them.
