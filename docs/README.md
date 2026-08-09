# Korg Kronos `.PCG`/`.SNG` File Format (reverse-engineered)

The full file-format internals reference -- container/chunk layout, SDB1/SBK1/CBK1/MBK1/
PBK1 record structures, Combi Timbre references, and the running list of open questions --
now lives in one place: **[`docs/content/format/index.md`](content/format/index.md)**, also
published at
**[jens-goes-mad.github.io/DIY-KORG-KRONOS-EDITOR/format](https://jens-goes-mad.github.io/DIY-KORG-KRONOS-EDITOR/format)**.

This file used to be a second, hand-maintained copy of that same content -- kept in sync by
hand with every edit, which drifted in practice (small inconsistencies crept in over time)
and cost real effort for no real benefit once the Hugo site existed. It's a short pointer
now instead, so there's exactly one place to update and no way for the two to disagree.

## What's covered there

1. Container format -- the chunk header shape, and how nested chunks work
2. Chunk hierarchy -- `PCG1 > {SLS1, PRG1, CMB1, ...}`
3. `SDB1` -- Set List names
4. `SBK1` -- per-slot parameters (Type, Color, Bank, Volume, Font size, Transpose, Comment)
5. Instrument-name cross-reference (`CBK1`/`MBK1`/`PBK1`)
6. Combi Timbre references
7. Notes from an external reference not yet used by this parser
8. Open questions (consolidated)
9. Where each piece is implemented in `src/kronos/`

## Everything else in `docs/`

- [`docs/content/`](content/) -- the Hugo site source (this file format page, the User
  Guide, App architecture, Building the app, ...). See
  [`docs/HUGO-SITE.md`](HUGO-SITE.md) for how to run it locally.
- [`docs/external/`](external/) -- community/vendor documents about Kronos internals
  (Korg's own SysEx docs, Synthify's writeups), with origin/license notes.
- [`docs/references/`](references/) -- independent third-party reverse-engineering
  projects of this same format, used as a cross-check.
