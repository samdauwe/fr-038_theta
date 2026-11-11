# Reference implementation

This project is a native C99 implementation of a demo whose engine has other implementations. They are the reference for the results the code
produces, and several tests are built on them. None of them is needed to build, run or test the player.

| Reference | What it is |
|---|---|
| Original source | The released werkkzeug3 C++ (`werkkzeug3/` of the [fr_public](https://github.com/farbrausch/fr_public) repository by Farbrausch, BSD), with the public-domain V2 player, the SGI GLU tessellator and the font outlines. The authority for arithmetic and behaviour; its Direct3D 9 side was not ported but re-implemented in OpenGL ES. |
| JavaScript implementation | An independent ES module implementation of the same engine (WebGL 2, Web Audio) that ties are decided against where the C++ cannot run here (Direct3D, MMX, x87). The hashes, vectors and call traces in `tests/data` were produced by it and are copied here unchanged. |
| Oracle | `tests/oracle/ogg_pcm.c` compares the decoder of `ogg.h` with the PCM of an independent decoder (ffmpeg). |

## Which code corresponds to which source

The headers follow the boundaries of the original files:

| Here | Original source |
|---|---|
| `wz3/prologue_detmath.inc` | fdlibm (the lineage of the browsers' `Math.sin/cos/atan/log2/pow`) |
| `wz3/core.inc` | `wz3/_types.*` |
| `wz3/bitmap_pixel.inc`, `bitmap_objects.inc`, `graph.inc` | `wz3/genbitmap.*` |
| `wz3/lookup_textures.inc` | `_start.cpp` (`MakeCubeNormalizer`, `MakeAttenuationVolume`) |
| `wz3/texture_bc3.inc`, `texture_levels.inc` | `_start.cpp::UpdateTexture`, `base/_rygdxt.cpp` |
| `wz3/class_meta.inc`, `kdoc.inc` | the compact K reader and the class table |
| `wz3/frame_types.inc`, `runtime.inc`, `runtime_tail.inc` | `kdoc.cpp`, `genblobspline.cpp` |
| `wz3/mesh.inc`, `mesh_edit.inc` | `genmesh.cpp` |
| `wz3/libtess.inc`, `font3d_glyphs.inc` | `libtess`, `font3d` |
| `wz3/minmesh.inc` | `genminmesh.cpp` |
| `wz3/frame.inc`, `scene.inc`, `effects.inc` | `genmaterial.cpp`, `genscene.cpp`, `geneffect*.cpp`, `genoverlay.cpp` |
| `wz3/render_plan.inc`, `material_view.inc`, `draw_list.inc`, `texture_plan.inc`, `geometry.inc`, `shadow.inc` | `engine.cpp` (Direct3D side) |
| `render/glr.h` | `engine.cpp`, the Direct3D materials |
| `core/v2.h` | v2redux (after kb's `synth.asm`) |
| `audio/ogg.h` | stb_vorbis |

Comments in the headers name the original files (`genmesh.cpp`, `kdoc.cpp`, `engine.cpp`, ...). Three kinds of reference remain, because the
original has no counterpart:

* "the reference implementation" marks behaviour taken from the JavaScript implementation (its number semantics, the canvas text adapter, the
  replay cadence of a seek) where the original cannot run here.
* "the reference renderer" (`glr.h`) is its WebGL renderer: the original draws with Direct3D 9.
* The tessellator sections of `wz3/libtess.inc` follow the file layout of libtess (`sweep.c`, `geom.c`, `mesh.c`, `tess.c`, ...), which is the
  SGI GLU sample implementation.

## Where the data comes from

| Here | Made by |
|---|---|
| `assets/theta.kx` | The document of the executable `fr-038-final.exe` (version 1.01, scene.org) behind a 16-byte header; details and checksums in [assets/ATTRIBUTION.md](../assets/ATTRIBUTION.md). |
| `assets/theta.ogg` | The soundtrack, embedded unmodified in the executable. |
| `tests/data/dxt5_vectors.txt`, `render_plan.txt`, `draw_list.txt`, `shadow.txt`, `glr_trace.txt` | The JavaScript implementation: hashes of BC3 blocks, planned renders, draw lists, shadow volumes, and the GL call stream of the renderer. The shader records in `glr_trace.txt` follow the text in `glr.h`. |
| `tests/data/theta/` | This project's own tools: the class histogram and the operator fingerprint of the document (`theta_inventory`, `theta_precalc`), the frame plans at five times (`theta_scene`) and the checksums of the assets. These are regression data, not oracles: they change when an operator changes and are updated on purpose. |

## Why two references

The C++ source is the truth, but several parts of it cannot be compiled here (Direct3D 9, MMX and x87 assembly, the original tessellator in its
Windows-only form). The JavaScript implementation was validated against the original's behaviour and runs the whole engine, so it provides the
end-to-end numbers: the hash of every operator output, the frame jobs, the GL call stream. A native result that equals both has two independent
confirmations.

Where the two differ, the C++ wins for arithmetic and the JavaScript implementation wins for the data it actually shipped to a browser.
