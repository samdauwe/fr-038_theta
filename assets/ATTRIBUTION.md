# fr-038 theta production data

The files below are derived from the party executable and are never edited by hand; `ctest` checks them against the SHA-256 recorded in
`tests/data/theta/assets.sha256`:

| File | Content | Origin |
|---|---|---|
| `theta.kx` | the compact werkkzeug3 document of the production: operator graph, animation bytecode, events, splines, blobs; 892,653 bytes, SHA-256 `036cf406812e86c214529bb98076947a342300dc3e22e3d840cd04064bdfa914`. The document is the executable's, byte for byte, behind a 16-byte header (layout flag, no embedded song, 138 BPM, 609 beats) | extracted from `fr-038-final.exe` (scene.org, "final version 1.01", SHA-256 `c11f0fbc6aa65776ea6ec706d8de0777b0f7d94a32378df484cb05ccc6847d8a`) |
| `theta.ogg` | the soundtrack, an Ogg Vorbis file, 44.1 kHz stereo, 259.6 s, 4,057,682 bytes, SHA-256 `1784669a4ee4dc53651e1452480e8bddc908fdb0d00cc2a8339b2c3b64c065d2` | embedded unmodified in the executable; byte-identical to `theta.ogg` of the released project |
| `eye.jpg` (inside `theta.kx`, operator 759) | the one imported picture of the production, 61,693 bytes, SHA-256 `ba3dcf07276feb924de6d94d254e5c9f8409b0d17e704bdfa2d91e1bdf9ce6b6` | embedded unmodified; byte-identical to `eye.jpg` of the released project |

## Licence

Farbrausch released the project files and music of fr-038: theta (party and final versions) under the
**Creative Commons Attribution-ShareAlike 3.0** license:

> This directory contains the project files and music for fr-038: theta
> (both party and final versions). All are released under a CC-BY-SA 3.0
> license:
> http://creativecommons.org/licenses/by-sa/3.0/

The files above are derived from that production and are distributed under the same license:
<https://creativecommons.org/licenses/by-sa/3.0/>. Share-alike applies to the data (and to any adaptation of it), not to the
source code of the player, whose licence is in [LICENSE](../LICENSE). The executable itself is not part of this repository.
The released project is at <https://github.com/farbrausch/fr_public/tree/original/werkkzeug3/data/theta>.

## Credits of the production

fr-038: theta, a paniq / cp / ryg / chaos coproduction, released at The Ultimate Meeting 2004 (final version 1.01).

* paniq: concept, music, graphics, design, direction
* cp: additional graphics, design
* ryg: main code, additional tweaking
* chaos: code
* fiver2, giZMo: werkkzeug3 co-development
* kb, yoda: namedropping

The production uses the werkkzeug3 engine, which is based on the .kkrieger engine.
