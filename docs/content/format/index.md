---
title: The .PCG/.SNG File Format
links:
  - title: Reverse-engineering the Korg Kronos backup file format
    description: container/chunk layout, Set Lists, Programs, Combis, and Combi Timbre references
menu:
    main:
        weight: 3
        params:
            icon: book

toc: true
---
This is the complete internals reference for the file format this project
parses. Korg has never published a single spec for this specific container
format, so the core of it was derived by hex-inspecting real backups and
confirming every offset against ground truth given directly (known
song/Combi/Program names, or deliberately-constructed test files that vary
one parameter at a time) -- nothing here is a plausible-looking guess left
unchecked. Alongside that, real external sources are used wherever they
exist and help: Korg's own SysEx/Parameter Guide documentation
(`docs/external/KORG/`, see `docs/external/README.md`) and an independent
third-party reverse-engineering effort
([DaBlick/PCG-Tools](https://github.com/DaBlick/PCG-Tools)), both
cross-checked against this project's own findings rather than trusted
blindly. Field names are our own working labels, not necessarily Korg's
internal terminology, unless stated otherwise.

Two files were used throughout: a real full backup (`20210504.PCG`,
~47.9MB) and two purpose-built test files we created
specifically to isolate individual fields (`setlist_test.PCG`,
`setlist_test_2.PCG`, the latter ~36MB and including full instrument-bank
data). Anything marked **CONFIRMED** below was checked against one or more
of these; anything else is a working hypothesis.

Status legend used throughout: **CONFIRMED** (checked against real/known
data), **assumed** (mechanically plausible, not independently verified),
**unknown** (not yet investigated).

---

## Status at a glance

A quick summary before the full section-by-section reference below -- moved here from the
[Overview](/overview) page, which now stays high-level and links here for the technical
detail.

- **Container format**: chunked, RIFF/IFF-like but big-endian -- every chunk has a
  fixed 12-byte header, `[4-char tag][u32 size][4-byte unknown field "dwX"]`, followed
  by exactly `size` bytes of content (a nested chunk's own size is counted in full
  toward its parent's, all the way down). `PCG1` itself is a real top-level chunk
  spanning the rest of the file, not implicit padding before the "real" content
  starts. Top-level children of interest: `SLS1` (Set Lists), `PRG1` (Programs, 20
  sub-banks), `CMB1` (Combis, 14 sub-banks) -- `DKT1`/`WSQ1`/`GLB1`/`DPI1` (Drum Kits,
  Wave Sequences, Global settings, and one unidentified chunk) exist but are unexplored.
  See §1-§2.
- **Set Lists** (`SDB1`): all 128 Set Lists, 128 song slots each, extracted correctly --
  verified against real user-named lists and real song titles given directly as ground
  truth, not guessed. See §3.
- **Per-slot parameters** (`SBK1`): Program-vs-Combi flag, bank, number, color, hold
  time, volume, Font size, Transpose, and a free-text comment, all at confirmed fixed
  offsets within a 542-byte-stride record -- Font size and Transpose are each a few bits
  packed into bytes otherwise used by Color/Bank, confirmed via a purpose-built test file
  that isolated each field one at a time. See §4.
- **Instrument name cross-reference** (`CBK1`/`MBK1`/`PBK1`): every Set List slot's real
  Program/Combi name, resolved and shown inline -- confirmed against three independent
  named anchors. See §5.
- **Combi Timbre-to-Program references**: each Combi's 16 Timbres sit at a fixed
  188-byte stride starting 4806 bytes into the Combi's own record: byte 0 is the
  referenced Program's number, byte 1 a raw bank code, byte 2 an on/off/engine-type
  status (Internal/External/Ex2/Off). All 20 Program banks now have a confirmed raw
  Combi Timbre bank code, plus 9 more permanently-indexless codes (`GM` and the "g(d)"
  family) confirmed by name only -- an absolute, gapped numbering scheme (not simple
  file order), fully mapped as of 2026-08-14. Independently cross-checked against a
  third-party reverse-engineering of this same format,
  [DaBlick/PCG-Tools](https://github.com/DaBlick/PCG-Tools) -- both sources agree at
  every point they overlap, and it resolved what first looked like a gap in this
  project's own model (turned out to be a Combi sample whose remembered state didn't
  match what was actually saved in the file, not a parsing error). See §6.
- **Factory "Init Program" template bytes**: the raw record Korg ships for every
  untouched Program slot (`Init Program` for HD-1 banks, `Init EXi Program` for EXi),
  extracted and cross-verified against two independent real backup files -- now used
  directly by the Duplicates panel's "keep this one" feature to clear the other copies
  of a duplicate back to a real blank slot. See §5.5.

Deliberately **not** solved yet: a handful of reserved bytes whose purpose isn't known
(byte +17 still has unexplained bits even after Font size/Transpose were found), Drum
Kits/Wave Sequences/Global settings, and whether a real backup can omit a Program/Combi
bank entirely (every file examined so far has had a complete, canonically-ordered set).
See §8 for the full, numbered list of open questions.

---

## 1. Container format

The file uses a chunked container similar in spirit to RIFF/IFF/AIFF, but
**big-endian** sizes throughout (Korg's synth DSPs are historically
Motorola-style big-endian, unlike Microsoft's little-endian RIFF).

### 1.1 Fixed 16-byte file header

```
offset  bytes                              meaning
0       "KORG"                             magic
4       0x68                               Product ID (identifies "Kronos" specifically) -- assumed
5       0x00                               format flag: 00=PCG, 01=SNG (assumed) -- our real sample is a
                                            .PCG file and reads 0x00 here, consistent
6       0x02                               Main Version -- assumed
7       0x01-0x02 (varies by file)         Minor Version -- assumed
8       0x00-0x01 (varies by file)         checksum flag: 00=none, 01=checksum present -- assumed,
                                            location of any actual checksum not investigated
9-15    00 00 00 00 00 00 00               reserved, all-zero in every sample seen so far
```

Byte meanings above are **assumed**, not independently derived -- sourced
from an external reference ([`DaBlick/PCG-Tools`](https://github.com/DaBlick/PCG-Tools),
see §7) and cross-checked against this project's own real file's actual
byte values, which are consistent with every claim (including the format
flag reading 0x00 on a real `.PCG` file). Nothing downstream in this
parser depends on these fields yet.

### 1.2 Chunk framing — CONFIRMED (2026-08-08, revises an earlier wrong model)

After the file header, the rest of the file is a sequence of chunks, each
with a **fixed 12-byte header**:

```
[4-char tag][4-byte big-endian size][4-byte unknown field "dwX"][size bytes of content]
```

**Confirmed** via `docs/external/Synthify-Kronos-PCG-File-Structures.xlsx`
(fetched independently, see §9's external-references list), which
describes exactly this structure in its own words: "A Chunk has a header
consisting of three 4-byte sized objects... TAG1... size... dwX... Data".
Cross-checked against this project's own byte-level findings via two
*other* independently-fetched official/community documents
(`docs/external/KORG/SetList.txt` and `CombiAndSongTimbreSet.txt`, Korg's
own SysEx parameter documentation) -- every field offset in those documents
lines up exactly with this project's own confirmed on-disk offsets once
this 12-byte header (not the old 8-byte one) is accounted for.

This **revises an earlier, wrong model**: this project previously believed
an extra 4-byte field of unknown purpose *preceded* the tag, ambiguously
(sometimes present, sometimes not), requiring a chunk header to be searched
for at two candidate positions. That was backwards -- the unknown field
(`dwX`) isn't a prefix before the tag at all; it's the *third* word of a
fixed, unambiguous header, right after `size` and right before the content
starts. The old model's "try position 0, then position+4" logic happened
to self-correct the resulting drift often enough (especially for deep,
unscoped searches like finding `MBK1`/`PBK1` bank chunks anywhere in the
file) that it went unnoticed for a while -- but a strict top-to-bottom walk
of a file's *top-level* chunks (`PcgFile::topLevelChunkTags()`, added for
the Internals pane) drifted out of sync after the first nested chunk with
the old math, which is what surfaced this. `dwX`'s own meaning is still
**unknown** -- only its presence, fixed position, and fixed width are
confirmed.

All tags found so far are 4 characters, first an uppercase letter,
remaining three uppercase letters or digits (`[A-Z][A-Z0-9]{3}`) --
`KORG`, `PCG1`, `DIV1`, `SLS1`, `SLD1`, `SDB1`, `STL1`, `SBK1`, `PRG1`,
`MBK1`, `PBK1`, `CMB1`, `CBK1`, `DKT1`, `WSQ1`, `GLB1`, `DPI1`, and
`DBK1`/`WBK1` (Drum Kit/Wave Sequence sub-banks, §7) and `INI1` (seen
once in the external reference, §7, purpose entirely unknown -- not
observed by this project directly yet).

A container chunk's own declared `size` covers *all* of its content,
including any chunks nested inside it -- i.e. for a chunk that's itself a
parent of children, the children's own full sizes (their own 12-byte
header plus their own content, recursively) sum to exactly the parent's
declared size, with no gap and no overlap. This is what lets a strict,
non-recursive walk of one level (e.g. `topLevelChunkTags()`) skip cleanly
from one top-level chunk directly to the next sibling, and what lets a
deep, unscoped recursive search (`collectChunks()`) find a chunk regardless
of how many levels it's nested at. Verified end-to-end via a synthetic test
fixture built with a *real* nested hierarchy (`PCG1 > SLS1 > (SDB1, SBK1)`,
`PCG1 > PRG1 > (PBK1, MBK1)`, `PCG1 > CMB1 > (CBK1)`, matching §2 below) --
deliberately reverting the header-size fix while keeping that nested
fixture makes the whole file fail to load at all ("No SDB1 chunk found"),
confirming this isn't just a cosmetic offset difference but the mechanism
a nested walk actually depends on.

## 2. Chunk hierarchy

```
PCG1                                -- whole-file container
 ├─ DIV1                            -- small fixed-size table, unknown, not decoded
 ├─ SLS1                            -- Set Lists (all 128 of them -- see §3, §4)
 │   ├─ SLD1
 │   │   └─ SDB1                    -- Set List NAMES (§3)
 │   └─ STL1
 │       └─ SBK1                    -- Set List per-slot PARAMETERS (§4)
 ├─ PRG1                            -- Programs: 20 sub-banks (§5.2)
 │   └─ MBK1 / PBK1  x20            -- interleaved in file order, 10 of each tag
 ├─ CMB1                            -- Combis: 14 sub-banks (§5.1)
 │   └─ CBK1  x14
 ├─ DKT1                            -- Drum Kits -- NOT explored
 ├─ WSQ1                            -- Wave Sequences -- NOT explored
 ├─ GLB1                            -- Global settings -- NOT explored
 └─ DPI1                            -- unidentified -- NOT explored
```

`PCG1` itself is a real, explicit chunk (its own 12-byte header, §1.2)
starting at byte 16, immediately after the 16-byte file header (§1.1) --
**not** implicitly consumed by that file header. Confirmed directly
against a real 36MB backup: `PCG1`'s own declared content size exactly
spans the rest of the file, and `DIV1`/`SLS1`/`PRG1`/`CMB1`/`GLB1` are its
direct children, one level inside its own content -- `topLevelChunkTags()`
(`PcgFile.cpp`) reads `PCG1` first and returns *its* children, not
whatever it finds naively starting at byte 16.

Only one `SLS1`/`SLD1`/`SDB1`/`STL1`/`SBK1` chain exists per file, but that
single chain holds all 128 of the unit's Set Lists internally (§3) -- this
is not a limitation.

`SLS1`'s total size is far larger than `SLD1` alone -- the remainder is
`STL1`/`SBK1` (§4), which fully accounts for it (no further mystery region
left inside `SLS1` once `SBK1`'s own header + 128 blocks are subtracted).

## 3. SDB1 -- Set List names — CONFIRMED

Found by grepping a real backup for known Set List/song names directly
(`"Rolling in the Deep"`, `"emergency exit"`, `"Wiener Hof Old Stars"`,
`"Misplaced Childhood"`) and reconstructing the byte layout around each
hit. This was necessary because the factory-preload data alone is
*misleading*: its slots are named after their demo engine/category (e.g.
`"HD-1"`, `"Combi"`), which looks like structural metadata rather than a
free-text name field until real user data is checked against it.

### 3.1 Header (8 bytes, at SDB1's content start) — CORRECTED 2026-08-08

```
offset  field                  sample value
0       u32be numSetlists      128       -- matches real hardware's 128 Set Lists
4       u32be bytesPerSetlist  3612      -- == 129 * 28
8..     `numSetlists` Set List blocks, `bytesPerSetlist` bytes each
```

**Was previously documented as a 12-byte header** with a leading `used`
field (sample value 344, "meaning unclear") before `numSetlists`. That
extra field never existed -- it was a misreading, caused by an unrelated
bug in this project's *chunk-level* header parsing (a 4-byte field, `dwX`,
that belongs to the chunk wrapper around SDB1, not to SDB1's own content --
see §1.2) that happened to shift every offset in this section by exactly
4 bytes in a way that canceled itself out end-to-end, so real Set List
names still decoded correctly despite the wrong field labels. Fixing the
chunk-level bug on its own (2026-08-08) broke this cancellation and
exposed the real, always-8-byte header underneath -- confirmed directly
against a real 36MB backup: `numSetlists`/`bytesPerSetlist` land exactly
where expected, and the whole 128-Set-List table (including all 5 real
user-named Set Lists in the exact confirmed order, §3.2) decodes correctly
with this header shape and no other. There are 4 bytes left over between
the last Set List block and the chunk's own declared end
(`bytesPerSetlist * numSetlists` is 4 bytes short of the chunk's full
content size) -- not yet understood, flagged rather than guessed at (see
open question list).

### 3.2 Set List block (129 x 28-byte records)

```
record 0        the Set List's own name
records 1..128   its 128 song/program slots, in order
```

Every record: `[4-byte marker][24-byte ASCII name, NUL-padded]`.

Three marker values seen:

| Marker | Meaning |
|---|---|
| `00 00 00 00` | Only on the very first record in the whole table (Set List #0's name). |
| `1e 02 00 00` | Ordinary marker -- a Set List's own name record (#2 onward), and most song slots. |
| `28 0f 01 00` | Appears on **exactly 128 records total** (one per Set List), always immediately after a name record -- flags "first song slot of a new Set List." This is the *only* way to find where one Set List's name ends and its 128 songs begin -- the name record itself is byte-for-byte indistinguishable from an ordinary record otherwise. |

Unpopulated song slots are empty strings (all-NUL after the marker) --
most factory-default Set Lists (`"Set List 005"` .. `"Set List 127"`) have
no songs assigned.

**Verified** end to end against the real 47.9MB sample: all 128 Set Lists
extracted correctly, including 5 real user-named ones (`Preload Set List`,
`emergency exit`, `Wiener Hof Old Stars`, `Misplaced Childhood`, `Pink
Floyd`) with real song titles (`Rolling in the Deep`, `Sex on Fire`,
`AC/DC`, `Africa`, `Purple Rain`, ...) and 123 untouched `Set List NNN`
defaults.

**A slot's position IS its order — CONFIRMED, no separate ordering field.**
`docs/external/KORG/SetList.txt` (Korg's own SysEx documentation) lays out
records 24..565 as "Slot 0", then states records 566..69399 are "Slot 1 ~
127 ... and the IDX is assigned to 1 ~ 127" -- i.e. a slot's on-hardware
number/order is simply which fixed-stride record it physically occupies,
with no separate "play order"/"next slot" pointer field anywhere in the
structure. This is the direct ground truth behind why this project's
reorder/copy operations (`PcgFile::reorderSong()`/`copySetlist()`/
`sortSetlist()`, see STATE.md) all physically move the 28-byte name +
542-byte params records themselves rather than writing to some lighter-
weight order/index field -- there isn't one to write to. It's also why
the editor's own on-screen A-Z/Z-A sort buttons (`frontend/pane.js`)
perform a real, immediate whole-Set-List rewrite rather than a display-
only convenience (an earlier version of this app got that wrong -- see
STATE.md's "Sort buttons corrected to a real reorder" entry): a real
Kronos loading this file plays Set List slots back strictly in raw record
position, so a *displayed* sort order would mean nothing to it -- the
only way to make actual hardware show a different order is to physically
write different bytes into those fixed positions, exactly what the sort
buttons now do.

## 4. SBK1 -- per-slot parameters — CONFIRMED (mostly)

Every SDB1 song record turned out to be **name-only** -- confirmed with a
purpose-built test file containing 4-6 identical-name slots per test
parameter, whose SDB1 bytes were 100% identical across the group, proving
no parameter data hides there. The real parameters live in `SBK1`, found
only by a *generic* chunk-tag scan (not a targeted SDB1-only search).

### 4.1 Header (same 8-byte shape as SDB1, see §3.1)

```
offset  field                  sample value
0       u32be numSetlists      128
4       u32be bytesPerSetlist  69,416  -- == 40 (header) + 128 * 542
8..     `numSetlists` Set List blocks, `bytesPerSetlist` bytes each
```

Previously documented as a 12-byte header with a leading `count` field
(347/470 seen across two files) -- same correction as §3.1, same cause
(canceled out against an unrelated chunk-level bug until 2026-08-08).
Confirmed directly against a real 36MB backup: `numSetlists`=128,
`bytesPerSetlist`=69,416 land exactly where expected with this 8-byte
shape, and all 128 Set Lists' worth of slot parameters (2,560 Programs /
1,792 Combis' worth of references, matching the real maximums exactly)
decode correctly.

### 4.2 Set List block

A 40-byte name/header record (same idea as SDB1's, more padding before the
first song), then 128 song records on a **542-byte stride**.

### 4.3 Song record layout (offsets relative to record start)

Confirmed by diffing `setlist_test.PCG`/`setlist_test_2.PCG`, in which the
project owner set up groups of 4-6 near-identical slots each varying
**exactly one** parameter and told us the exact values used:

| Offset | Field | Encoding | Confirmed via |
|---|---|---|---|
| +12 | Type + Color + Font size (low 2 bits) | bits 0-1 = Type (0=Combi, 1=Program, 2=Song); bits 2-5 = `4*(color-1)`, 1-based color (this byte's bits 0-5 only -- see §4.4 note); bits 6-7 = Font size's low 2 bits, see §4.4 | Color values `1,2,4,16` -> byte `1,5,13,61`, exact match. Type: **CORRECTED 2026-08-08** -- was documented (and coded) as a single bit (bit0 only, 1=Program/0=Combi); Korg's own SysEx documentation (`docs/external/KORG/SetList.txt`) confirms it's 2 bits, and this project's own code was accidentally still correct for Program-vs-Combi specifically (bit0 alone distinguishes those two), just not for Song (a slot this project has still never observed in a real file) and the unused value 3. |
| +13 | Bank + Transpose (high 3 bits) | bits 0-4 = raw bank index (see §5); bits 5-7 = Transpose's high 3 bits, see §4.4 | see §5 |
| +14 | Number | program/combi number within that bank (0-127) | see §5 |
| +15 | Hold Time | `byte = HoldTime + 1` | Values `1,2,3,5` -> byte `2,3,4,6`, exact match. Default/baseline byte value 6 => default Hold Time is 5. |
| +16 | Volume | raw 0-127, MIDI-style, no transform | Values `0,1,80,127` matched exactly. Default/baseline is 127. |
| +17 | Font size (high bit) + Transpose (low 3 bits) + Keyboard Track | bit 4 = Font size's high bit, see §4.4; bits 5-7 = Transpose's low 3 bits, see §4.4; bits 0-3 = Keyboard Track (track 1-16), per `docs/external/KORG/SetList.txt` -- not yet decoded anywhere in this app (nothing needs it yet). Bits 0-2 specifically still not independently isolated by this project's own test files. | see §4.4; Keyboard Track per Korg's own SysEx doc, 2026-08-08 |
| +18.. | Comment | free ASCII text, **512 bytes max** (confirmed 2026-08-08, was assumed to run to the record's own end, 524 bytes), can contain literal `\r\n`; NUL-terminated only if shorter than the full 512 bytes (a full-length comment has no terminator, same convention as this format's name fields) | Multiple test comments matched exactly, incl. multi-line ones; length cap per `docs/external/KORG/SetList.txt` |

Bits 6-7 of +12 and bit 4 of +17 (Font size), and bits 5-7 of +13 and +17
(Transpose), overlap with fields already documented above (Color, Bank) --
**always mask to the bits you actually own when reading or writing any of
these four fields**. A naive full-byte read of Bank/Color would silently
produce wrong values on any real slot that also has a non-default Font
size or Transpose set. Presumably Korg packed fields this tightly because
the format predates spare bytes being cheap -- see the open questions
list for what bit 3 and bits 0-2 of +17 might still be doing.

### 4.4 Font size and Transpose — CONFIRMED

Both isolated and confirmed via a purpose-built test file (Set List 127,
slots 0-4 for Font size, slots 8-19 for Transpose -- confirmed to be
properly isolated this time, unlike the earlier Font size false start,
see below) with the exact real values used confirmed on real hardware.
Both fields turn out to be a handful of bits packed across two otherwise-
unrelated bytes rather than living in one clean byte of their own.

**Font size** -- 3 bits, split across +12's top 2 bits and +17's bit 4:

```
value = ((byte17 >> 4) & 1) * 4 + ((byte12 >> 7) & 1) * 2 + ((byte12 >> 6) & 1) * 1
0 = S (the true baseline/default -- zero extra bits set)
1 = XS
2 = M
3 = L (== M's bit | XS's bit, i.e. both of the other two bits set)
4 = XL
```

**Transpose** -- a 6-bit two's-complement signed value (range -32..+31),
high 3 bits in +13's top 3 bits, low 3 bits in +17's top 3 bits:

```
unsigned6 = ((byte13 >> 5) & 0x7) << 3 | ((byte17 >> 5) & 0x7)
transpose = unsigned6 >= 32 ? unsigned6 - 64 : unsigned6
```

Verified against all 12 of our real test values
(`-24, -23, -12, -11, -10, -1, 0, 1, 11, 12, 13, 24`) -- every single one
round-trips exactly through this formula, no exceptions.

**Why the earlier attempt failed**: the original "Font size" observation
(`0x41, 0x01, 0xc1, 0x01`, recorded as unsolved) was never from isolated
Font-size-only slots at all -- confirmed on real hardware. Those
byte values are exactly the Color-sweep test's own edge-case slots
(colors 17, 1, 49, 1 -- byte `+12`'s bits 6-7 set to non-zero, which the
plain Color formula misreads as "colors beyond the documented 1-16
range" precisely because those bits are Font size's, not Color's). The
old Transpose partial fit (`byte(+16-ish) = (transpose*32) mod 256`,
matched only for small values) is also now explained: that's `+17`'s top
3 bits, the low half of the real 6-bit value -- it looked right for
small transpose values because the high 3 bits (in `+13`) happened to
still be 0 for those, and broke down for larger values because the high
half lives in a completely different byte.

### 4.5 Color names — CONFIRMED

The `color` field (+12, bits 2-5, see §4.3) is 1-based, 1-16. Confirmed
against real Kronos hardware (2026-08-06, via `tools/
generate_setlist_test_matrix.{js,cpp}`'s Group 5, one real color written
to a Set List slot per value 1-16, checked by eye on the device) -- and
substantially different from this project's earlier working guess, which
used generic named colors ("Red"/"Blue"/"Black"/"White"...) in what turned
out to be the wrong order too. The real palette is Korg's own curated,
muted set -- none of the generic names exist at all:

| Value | Name | Hex (as read off the device) |
|---|---|---|
| 1 | Default | `#494c55` |
| 2 | Charcoal | `#282b31` |
| 3 | Brick | `#af4350` |
| 4 | Burgundy | `#661b27` |
| 5 | Ivy | `#929a33` |
| 6 | Olive | `#233519` |
| 7 | Gold | `#aa8c3e` |
| 8 | Cacao | `#723d3f` |
| 9 | Indigo | `#3759bf` |
| 10 | Navy | `#0410ab` |
| 11 | Rose | `#9478c7` |
| 12 | Lavender | `#745ad2` |
| 13 | Azure | `#5588c2` |
| 14 | Denim | `#385f9c` |
| 15 | Silver | `#546180` |
| 16 | Slate | `#2a3149` |

Hex values are our own on-screen reading, not a
pixel-sampled measurement -- close enough to use directly (this app's UI
brightens them a bit further for on-screen legibility, a purely cosmetic
display-time adjustment, see `frontend/pane.js`'s `brightenHex()`), but not
guaranteed pixel-perfect. **Open question, not yet investigated**: whether
this palette (names and/or hex) is identical across every Kronos hardware
variant/revision, or whether it differs by model (e.g. an original unit vs.
a limited/"silver edition" unit) -- confirmed so far only on the one unit
this project has access to.

## 5. Instrument-name cross-reference — CONFIRMED

An SDB1 song name is just a label -- it can be (and often is) edited
independently of the actual Program/Combi it points to. The *real*
instrument banks are top-level siblings of `SLS1` inside `PCG1` (found by
scanning the whole file's top level, not just inside `SLS1`).

`CBK1` (Combi) and `MBK1`/`PBK1` (Program) banks all share **one record
shape**:

```
offset  field                     Combi value   Program value
0       u32be numRecords          128           128
4       u32be bytesPerRecord      7810          4960
8..     `numRecords` records, `bytesPerRecord` bytes each
```

**Corrected 2026-08-08** (was previously documented as a 12-byte header
with a leading "count (unknown)" field) -- same fix, same cause, as
§3.1/§4.1's identical correction: the extra field never existed, it was a
misreading canceled out by an unrelated chunk-level bug until that bug was
fixed. Confirmed directly against a real 36MB backup with this 8-byte
shape: all 20 Program banks and all 14 Combi banks decode with
`numRecords`/`bytesPerRecord` exactly as expected, and the resulting
Program/Combi counts (2,560 / 1,792) exactly match the real physical
maximums (20×128 / 14×128). Same unexplained 4-byte gap as §3.1 between
the last record and the chunk's own declared end.

**Confirmed directly** (real Kronos hardware behavior we confirmed
2026-08-07, not derived from parsing): a bank is the
unit's atomic storage granularity -- its capacity is a fixed 128 slots,
and a bank is always either fully populated or entirely absent from a
given backup, never partially saved. This is why `numRecords` above reads
exactly 128 for every real Program/Combi bank this project has ever
parsed, rather than something that just happened to be 128 in the files
examined so far. It also sharpens what "a bank is missing from this
backup" can mean in practice -- see the [Internals pane](/overview) and
open question #13 below: the only real absence a backup can have is a
whole bank chunk missing entirely, never a partial one.

Each record's name is a **fixed 24-byte field starting 4 bytes into the
record** -- space/NUL-padded, but **not NUL-terminated**: a full-length
24-character name has no terminator at all, so trailing NUL/space must be
trimmed rather than scanned-for. `parseNamedBanks()` in `PcgFile.cpp`
handles both bank types uniformly; a slot's `bank`/`number` (from SBK1,
§4.3) directly index `[bank][number]` into whichever list matches its
type. Whether a bank is tagged `MBK1` or `PBK1` turned out to be
irrelevant to name lookup -- just two tag values for the identical record
shape. The tag itself is understood (via an external reference, §7) to
signal which sound engine that bank's Programs use; see §5.2 below.

### 5.1 Combi banks (`CMB1 > CBK1`) -- 14 banks

Bank order (file order == this list, **CONFIRMED**):

```
0  INT-A     4  INT-E      8  USER-B    12  USER-F
1  INT-B     5  INT-F      9  USER-C    13  USER-G
2  INT-C     6  INT-G     10  USER-D
3  INT-D     7  USER-A    11  USER-E
```

### 5.2 Program banks (`PRG1 > MBK1`/`PBK1`) -- 20 banks

Bank order (file order == this list). **CORRECTED 2026-08-10**: this used
to assume `INT-A..G` (7 letters, indices 0-6) followed by a `G(d)` filler
before `USER-A` at index 8. The project owner checked directly on real
Kronos hardware: there is no `INT-G` bank at all -- what the unit shows
after `INT-F` is `GM`, then `g(d)`, neither of which is a stored PBK1/MBK1
chunk (consistent with §5.4 below). `USER-A` starts right after `INT-F`,
at index 6, confirmed both by name (`INT-F`'s own real slot 0, "Doubled
Screamer") and by a full round-trip: Combi U-A 016 Timbre 2 (raw bank 17,
raw number 47) had been resolving to the wrong Program ("Xfade
StagePianoATK Kn5", the old index-8 reading) until this fix -- index 6,
record 47 is "EXi Overdrive Organ", exactly what we
confirmed on the unit. USER-D/F/AA were independently confirmed the same
way (index 9/11/13, matching "Vibraphone 2"/"Harmonic Bass/Lead"/"The
Temple SW1" on real hardware). That also means `USER-A..G` is genuinely 7
single-letter banks, not 6 -- resolving the `USER-G` contradiction flagged
in §6.2:

```
0  INT-A      6  USER-A     13  USER-AA
1  INT-B      7  USER-B     14  USER-BB
2  INT-C      8  USER-C     15  USER-CC
3  INT-D      9  USER-D     16  USER-DD
4  INT-E     10  USER-E     17  USER-EE
5  INT-F     11  USER-F     18  USER-FF
           12  USER-G     19  USER-GG
```

**All 20 indices confirmed 2026-08-11**: we checked every
single position-0 Program name in the app against the real Kronos's own
on-screen bank browser, for the entire list above -- every one matched
exactly (`INT-A..F`, `USER-A..G`, `USER-AA..GG`), including the ones this
document previously listed as unconfirmed (`USER-B/C/E` and
`USER-BB/CC/DD/EE/FF`). This confirms the bank *order/labels* fully -- it
does not, on its own, confirm a raw Combi Timbre code for the codes still
missing one (`USER-E`, `USER-BB/EE/FF`, see §6.2); those need their own
independent raw-byte check against real hardware, same as `USER-B/C`
already have (name only, no index needed for that -- see §6.2).
`frontend/pane.js`'s `PROGRAM_BANK_NAMES` -- the app's single source of
truth for this list (see below) -- had drifted out of sync with this
correction until this same check caught it: index 6 read `"I-G"` and
index 7 read `"G(d)"` there, an old leftover from the pre-2026-08-10
scheme, so labels 6-19 were all still off by 2 in the actual UI even
though `PcgFile.cpp`'s Combi-Timbre-code table had already been fixed --
fixed the same day.

Note `GM` itself is *not* one of these 20 stored banks -- bank values
`>=20` seen in real slot data don't correspond to anything stored per-file
(see §5.4), consistent with `GM` being fixed MIDI-spec content Korg
doesn't need to store, rather than the 21st item in this list.

**Confirmed directly** (2026-08-07): which sound engine a
Program bank uses (HD-1 vs EXi, see §7's `MBK1`/`PBK1` note) is a *global,
per-bank* setting on the unit itself -- Programs within one bank can't
individually be assigned to different engines. Combined with the
whole-bank-or-nothing storage granularity noted above, this means a
Program bank chunk found in a file is always a complete, single-engine
128-slot unit; this project's own `ProgramBankType`
(`src/kronos/PcgFile.h`) is tracked per bank for exactly this reason,
never per record. (This confirms the *behavior* -- one engine per bank, no
partial banks -- not the specific `MBK1`=EXi/`PBK1`=HD-1 tag mapping
itself, which remains externally sourced only; see §7.)

### 5.3 Verification anchors (ground truth given directly, not guessed)

| Anchor | Type | Location found | Result |
|---|---|---|---|
| "Rolling in the Deep" | Combi | bank 7 (USER-A) / record 9 | Exact match -- Set List slot name, Combi name, and our own stated bank/number all agree |
| "Berlin Grand SW2 U.C." | Program | PRG1 bank 0 / record 0 | Exact match |
| "Rain Again" | Program | PRG1 bank 0 / record 127 | Exact match |
| "Subdivisions", "Perfect Kiss", "Sirius" | Program | PRG1 bank 0 / records 90, 91, 92 (consecutive) | Exact match -- confirmed Program uses the identical record layout/mechanism as Combi |
| "KARMA INTERNAL COMBI" | Combi | banks 7 & 8, several records | Matched a real (placeholder/default) Combi name -- confirms those banks parse correctly, though this specific name recurs as a generic default, not a unique identifier |
| "Dont stop believin" | Combi | bank 7 / record 4 | Exact match |

Across a full pass of a real-ish test file (`setlist_test_2.PCG`), 143 of
152 assigned slots resolved to a name; all 9 misses had a bank value
outside the stored range (§5.4) -- **zero** in-range lookups failed.

### 5.4 Bank values outside the stored range

Program bank values `>=20` and Combi bank values `>=14` seen in real slot
data don't correspond to any stored bank (there are only 20/14
respectively). These are near-certainly `GM`/`GM2` references (fixed
content per the MIDI spec, not stored per-file) rather than a parsing
bug. A one-off bank-231 (Combi) and bank-192 (Program) reference were also
seen once each in real data -- more likely genuine data corruption/an
edge case in that one specific slot than anything this parser mishandles.
All out-of-range cases are left showing a raw `bank-number` rather than a
guessed label, both for the name (empty, degrading gracefully) and the
UI's bank label.

Note this is a *different* number space from a Combi Timbre's own
`rawBankCode` (§6.2) -- both ultimately point at the same real-world
concept (`GM` being fixed MIDI-spec content, not stored per-file), but a
Song/Program slot's `bank>=20` signal and a Combi Timbre's confirmed
`rawBankCode=6="GM"` are two separate bytes in two separate record types,
confirmed independently of each other. Don't conflate them.

### 5.5 Record size — CORRECTED 2026-08-13, and the factory "Init Program" template

`PcgFile::copyProgramFrom()`'s own doc comment used to claim HD-1 records are
4960 bytes and EXi records are 3706 bytes. The EXi figure was never actually
checked against real bytes. Confirmed against `programBankInfo()` over two
independent real backup files (`setlist_test_2.PCG` and `test_1.PCG`): every
one of the 20 PRG1 sub-banks, HD-1 or EXi alike, uses 4960-byte records. The
comment in `PcgFile.h` is corrected; `RecordSizeMismatch` in
`copyProgramFrom()` is kept regardless as a belt-and-suspenders check, since
a third real file could yet show a genuine stride difference this project
hasn't hit.

A new `PcgFile::programRecordBytes(bank, number)` accessor (mirrors
`songRecordBytes()`/`nameRecordBytes()`'s shape) exposes one Program's raw
record directly. First use: extracting Korg's own factory-default "Init
Program" (HD-1) and "Init EXi Program" (EXi) record bytes as this app's own
known-good template for a future "clear a Program slot" feature -- see
`resources/Init-Program-HD1.raw`/`Init-Program-EXi.raw`, both 4960 bytes,
extracted from a representative slot in `setlist_test_2.PCG` and confirmed
byte-identical against the same two names' records in the independently-
different `test_1.PCG`. That write path exists now (see `STATE.md`
entries 32/33): the Duplicates panel's "keep this one" button clears every
OTHER duplicate to the matching template and repoints Combi/Set List
references to the kept slot.

**Name field deliberately customized (2026-08-14)**: once the write path
was real, the stored bytes' actual 24-byte name field (Korg's own real
factory content, `"Init Program"`/`"Init EXi Program"`) turned out too
subtle in the UI -- a cleared slot looked identical to any other
already-blank one. `resources/Init-Program-HD1.raw`/`Init-Program-EXi.raw`
now carry `"- Init Program (HD1) -"`/`"- Init Program (EXi) -"` in that same
24-byte field instead (22 characters each, fits with room to spare -- the
field is a hard 24-byte limit, confirmed, not a guess), so a cleared slot
reads as unmistakably different at a glance. Every other byte in both
files -- including the still-unresolved Tone Adjust value below -- is
still the real, cross-verified extracted content; only the name field was
touched.

While verifying: every "Init Program"/"Init EXi Program" slot is byte-
identical to every other slot of the same name **within its own bank**, but
bytes 2632-2633 differ consistently **across** banks (e.g. bank 12 vs bank
17, both HD-1) -- first suspected as a per-bank identity tag baked into the
record. Cross-checked against Korg's own official parameter reference
(`docs/external/KORG/Prog_HD-1.txt` and `Prog_EXi_Common.txt`, identical
entry in both): it's "Tone Adjust" / "Switch8 On Value", a real Program
parameter, nothing bank-identity-related. Still an open practical question
for the planned Duplicates feature above: a factory Init Program's Tone
Adjust value isn't identical across every bank, so writing one bank's
template into a *different* bank's slot would carry over whichever value
that source bank's Init Program happened to have -- not yet checked whether
that reads as harmless on real hardware.

## 6. Combi Timbre references — CONFIRMED (Program refs), status byte CONFIRMED

Each Combi record (`CMB1 > CBK1`, §5.1) has 16 Timbre slots, each optionally
referencing a Program. Confirmed with several
real Combis with known Timbre->Program assignments, and independently
cross-checked against a third-party reverse-engineering of this format
([DaBlick/PCG-Tools](https://github.com/DaBlick/PCG-Tools), see
[docs/references](https://github.com/jens-goes-mad/DIY-KORG-KRONOS-EDITOR/blob/main/docs/references/README.md)) --
both sources agree at every point they overlap.

### 6.1 Layout

16 fixed-size 188-byte blocks starting 4806 bytes into the Combi's own
record (i.e. `recordOffset + 4806 + timbreIndex * 188`), regardless of how
many Timbres are actually in use -- this stride does **not** vary with
content, an earlier "variable-length" theory was tested and disproven.
Each block's first 3 bytes:

```
offset  field
0       Program number (0-127), raw byte
1       raw bank code (see §6.2 -- NOT the same index space as §5.2's
        Program bank list; some other, absolute Kronos-internal numbering)
2       status byte -- top 3 bits ((byte >> 5) & 0x07): 0=Off, 1=Internal,
        3=External (MIDI), 4=Ex2 (expansion board). Lower 5 bits are a
        separate, unrelated field: the Timbre's own 0-based index (0..15),
        confirmed by watching it count up regardless of status.
```

**Off does not imply "no reference stored"**: a Timbre can hold a genuine,
non-zero Program number/bank while its status is Off (e.g. temporarily
disabled without clearing the assignment). The all-zero
number=0/bank=0/status=Off pattern seen on every genuinely-untouched
Timbre slot is a *separate* signal (`TimbreRef::isDefault` in
`PcgFile.h`) from the on/off status (`TimbreRef::status`) -- both are
tracked independently rather than collapsed into one flag, since anything
usage-counting-related (e.g. "is this Program safe to delete") should
probably still count an Off-but-referenced Timbre as a real reference.

### 6.2 Confirmed raw bank codes

```
0   INT-A       17  USER-A     24  USER-AA
1   INT-B       18  USER-B     25  USER-BB
2   INT-C       19  USER-C     26  USER-CC
3   INT-D       20  USER-D     27  USER-DD
4   INT-E       21  USER-E     28  USER-EE
5   INT-F       22  USER-F     29  USER-FF
                23  USER-G     30  USER-GG
```

All 20 Program bank indices now have a confirmed raw Combi Timbre code
(`USER-FF` was the last gap, resolved 2026-08-14 -- see below).

Every code above is a directly-verified byte value (from a real Combi
sample, the external reference, real hardware, or several of these
together) -- not an extrapolation.

These 20 codes are a *different number space* from this project's own PBK1
file-order Program bank index (`ProgramInfo::bank`) -- they coincide for
INT-A..F (all six happen to use 0..5) but diverge for the other 14 (e.g.
USER-D is file-order index 9, but Timbre code 20). `PcgFile.cpp`'s
`kConfirmedTimbreBanks` table pairs each confirmed code with its file-order
index so Combi-usage counting (`combiUsagesForProgram()`/
`combiUsageCounts()`, backing the Programs table's `#CMB` column, the
Program usage panel, and Duplicates' per-copy reference counts) can
translate between the two and cover all 20 confirmed banks correctly, not
just the INT-A..D range where the numbers happen to match (fixed
2026-08-08 -- previously only INT-A..D actually fed into usage counting,
even though USER-A/D/F/AA's codes were already sitting right here,
confirmed but unused for that purpose). **CORRECTED 2026-08-10**:
USER-A/D/F/AA's file-order indices were originally 8/11/13/14 -- see
§5.2's own note above for the full derivation of the real 6/9/11/13.
`USER-G` (index 12) and `USER-GG` (index 19, resolving the raw-code-30
question below) were added the same day once independently confirmed by
name against real hardware too.

**Promoted 2026-08-11**: `INT-F`/`USER-B`/`USER-C`/`USER-CC`/`USER-DD`
(codes 5/18/19/26/27) used to sit in a separate name-only table -- their
raw code was directly confirmed (checked real Combis against real Kronos
hardware: Combi U-A 002 Timbre 2 -> code 5, verified `INT-F`; Combi U-A 000
Timbre 0 -> code 26, verified `USER-CC`; `USER-B`/`USER-C`/`USER-DD`
confirmed the same way against other real Combis), but the matching PBK1
file-order index wasn't, so they didn't participate in Combi-usage
counting's index<->code translation and their Program name never showed
in the Combi Timbre list at all (`isConfirmedTimbreProgramBank()` returned
false, so `library.js`'s `formatTimbreRef()` skipped the name lookup
entirely -- e.g. Combi U-A 002 "Sex on Fire" Timbre 2, raw bank 5/`INT-F`,
showed the bank label but no Program name). Once §5.2's full 20-bank order
got independently confirmed against real hardware (below), all five turned
out to already have a confirmed index too (5/7/8/15/16) -- promoted into
`kConfirmedTimbreBanks` directly, and the reported bug fixed: index
5/record 71 in `setlist_test_2.PCG` reads "Vokal Dancing", matching
"Vocal Dancing" on the real unit for that exact Combi Timbre. The
now-empty name-only table (`kConfirmedTimbreBankNamesOnly`) was removed
entirely rather than left around empty.

**Raw code 30 = `USER-GG`, RESOLVED 2026-08-10**: real bytes from
`setlist_test_2.PCG` (Combi U-A 016, Timbre 3: raw program 15, raw bank
30) pointed at file-order index 19/record 15, which reads "JMJ THEREMIN"
in that file -- and we independently confirmed, by directly
browsing Program bank `USER-GG` position 15 on real hardware, that it
really is "JMJ Theremin". Two independent paths (the raw Combi Timbre
bytes, and a direct Program-bank browse) landing on the same name is
enough to confirm this the same way as the rest of this table. An earlier
hypothesis that raw code 30 meant `INT-D` (bit-sharing with code 3) was
tested directly against real byte data first and found unsupported (Bank
MSB/LSB were always 0x00 for both, no other byte anomaly) -- worth noting
as a case where the *first* hypothesis tested was wrong, and only got
resolved once a full round of ground-truth checking worked through the
whole revised §5.2 bank order.

**USER-BB/EE confirmed (2026-08-11); INT-E/USER-E RESOLVED, retracting an
earlier misreading (2026-08-14)**: checked directly against real Combis in
`setlist_test_2.PCG`. `USER-BB` (25) and `USER-EE` (28) fit the expected
`+11`-offset pattern exactly (Combi I-A 000 "K-Lab: Katja's House" Timbre
9/index 8 has raw program=29/bank=25; Combi U-A 014 "KARMA Org 1'2'3
Piano 4" Timbre 7/index 6 has raw program=1/bank=28). This section briefly
claimed `USER-E` was raw code 4 -- "a genuine surprise" breaking the
contiguous 17-23 block every other single-letter USER bank sits in --
based on our own real-hardware report for the same
"K-Lab: Katja's House" Combi's Timbre 7/index 6 (raw program=61/bank=4).
That report was a misreading of the unit's display: re-checking the exact
same Timbre confirmed real hardware actually shows `INT-E`, not `USER-E`,
for that reference. So there was no anomaly at all -- `INT-A..F` are
simply raw codes 0-5 in order (index == code for all six, extending the
same coincidence `INT-A..D` already had), exactly what the "obvious"
extrapolation always said. `USER-E` is confirmed separately, via a
different real Combi (I-A 001 "Stradivarius Goes POP" Timbre 7, raw
program=73/bank=**21**) -- and 21 turns out to be exactly the "obvious"
gap in `USER-A..G`'s 17-23 block after all (A=17,B=18,C=19,D=20,E=21,
F=22,G=23, fully contiguous). Left as a methodology note rather than
scrubbed from history: the original "genuine surprise" framing was itself
the mistake, caught only because we re-verified a specific
real-hardware reading instead of trusting the first transcription --
exactly the kind of double-check this project's whole method depends on.
`USER-FF` was the one remaining unconfirmed double-letter code, and the
*only* one of the 20 Program bank indices with no confirmed raw Combi
Timbre code at all -- checked directly (not assumed at 29 just because the
rest of the series fit `+11` cleanly, given the `INT-E`/`USER-E`
precedent) via a real Combi (U-A 090 "Days like this" Timbre 1/2,
program=87/bank=29, program=85/bank=29), **RESOLVED 2026-08-14**: it
really is `USER-FF`, exactly as the pattern predicted this time. All 20
Program bank indices now have a confirmed raw Combi Timbre code -- this
table is complete.

**One name, one place (2026-08-11)**: `kConfirmedTimbreBanks` used to also
carry a `name` string per entry, redundant with §5.2's `PROGRAM_BANK_NAMES`
for every index the two tables share -- which is exactly how they drifted
apart on 2026-08-10 (this table's indices got corrected without anyone
noticing `PROGRAM_BANK_NAMES` still had the old wrong order, since nothing
forced the two to agree). Removed the `name` field instead of just
re-syncing the strings: `library.js`'s `formatTimbreRef()` looks up
`PROGRAM_BANK_NAMES[programBankIndex]` for every entry in
`kConfirmedTimbreBanks` instead of reading a name off the bridge.
`timbreBankName()` resolves a name only for a raw code confirmed by name
but with NO PBK1 index (`kConfirmedTimbreBankNamesOnly`) -- removed on
2026-08-11 as permanently empty (every entry in it at the time had gained
a confirmed index once §5.2's full order was known), then reintroduced the
next day once a genuine counterexample turned up (`GM`, below) --
confirming the "unlikely but not structurally impossible" case really can
happen. `frontend/mock_bridge.js`'s fake Timbre data matches this contract
too, including one `GM` example.

**`GM` (raw code 6), PERMANENTLY indexless, confirmed 2026-08-12**: Combi
U-A 030 "Bad Name" Timbre 2 -- raw bytes program=91/bank=6 in
`setlist_test_2.PCG`, confirmed on real hardware
exactly ("code 6 - 091" -> `GM 092`). Unlike every other confirmed code
above, `GM` is not "not yet" indexed -- it structurally can never be:
`GM` is fixed MIDI-spec content, not one of the 20 stored PBK1/MBK1
Program banks at all (§5.2/§5.4), so there is no PBK1 file-order index to
ever confirm for it. Shown as a bare "GM" bank label with the raw number,
no Program name -- there's nothing in this file's own `programs` array to
look one up from (that would need a hardcoded General MIDI
instrument-name table, a real but separate feature decision, not implied
by just labeling the bank). No jump-to-Program button either, for the same
reason -- `library.js`'s Timbre-bank-jump button only renders when a
confirmed PBK1 index exists.

**`G(1)`..`G(4)` (raw codes 7-10), same treatment, confirmed 2026-08-12**:
Combi I-C 022 "Rainbow Bridge" (our early note said "Rainbow
Brodge" -- a typo, not a different Combi; the real name matches exactly)
Timbres 1-4 -- raw bytes program=122/bank=7, program=122/bank=8,
program=122/bank=9, program=122/bank=10 in `setlist_test_2.PCG`, all four
confirmed on real hardware exactly. These sit
right after `GM` (6) as a contiguous block, consistent with §5.2's own
note that the real Program bank browser shows "GM" then "g(d)" right
after `INT-F` -- very likely that same "g(d)" family (four separate
banks?), though this project doesn't know Korg's own official name or
purpose for `G(1)`..`G(4)` specifically, and isn't guessing. Same
permanently-indexless treatment as `GM`: no PBK1 index, no Program name,
no jump button. We also recorded the specific Program
names found there (program 122/"123": "Rain"/"Thunder"/"Wind"/"Stream" for
`G(1)`/`G(2)`/`G(3)`/`G(4)` respectively) -- useful as confirmation these
are real, distinct, content-bearing banks (and a suggestive hint they
might be General MIDI Level 2's SFX/nature-sound kits), but these
per-program names aren't stored anywhere in this codebase; showing them
would be the same separate feature decision as `GM`'s own instrument names
above.

**`g(5)`/`g(6)`/`g(7)`/`g(9)` (raw codes 11/12/13/15), same treatment,
confirmed 2026-08-13**: extend the same contiguous block right past
`G(4)`, verified against real Combis in `setlist_test_2.PCG` (Combi I-B
055 "Prehistoric Predator" Timbres 5/6, Combi I-B 039 "Planetary
Explosion" Timbre 5, Combi I-A 096 "Guitar Hero" Timbre 4). `g(8)` (code
14) has NOT been checked -- not assumed just because the run around it
fits. Lowercase this time, matching exactly what was confirmed on real
hardware (`G(1)`..`G(4)` were reported uppercase) -- kept verbatim rather
than normalized, since it isn't confirmed which casing (if either
consistently) the real Kronos UI uses. The reported Program names
("Bubble"/"Seashore"/"Jetplane"/"Polyphonic Synth") continuing right after
`G(4)`'s "Stream" in the same nature/SFX theme is suggestive of a General
MIDI 2 SFX Kit note sequence (Rain/Thunder/Wind/Stream/Bubbles/... is a
real, externally documented GM2 order) -- worth noting, not asserted as
confirmed without checking that specific external spec directly.

**`code 21` = `USER-E`, RESOLVED 2026-08-14** -- see the `INT-E`/`USER-E`
entry above for the full story (this section briefly flagged a conflict
between this and an earlier, mistaken `USER-E`=code-4 report; the
mistaken report was retracted, not code 21). Worth noting as its own
methodology lesson: code 21's sheer usage count (over 1,200 occurrences
across the sample files, by far the most of any previously-unidentified
code) was floated here as a hint it might be a heavily-used bank like the
still-unconfirmed `INT-E` rather than a `USER-*` bank -- that guess was
also wrong. Frequency alone isn't reliable evidence either; `USER-E`
really is that common in practice.

### 6.3 A resolved "anomaly" (worth recording as a methodology note)

An early Combi sample ("061 Sledgehammer") appeared to contradict this
model: Timbre 3 and 4's raw bytes didn't match the Program numbers we
recalled from memory, and Timbres 5-9 (which we
believed were "active") read as all-zero/Off. Decoding the status
byte resolved this completely: Timbres 5-9 in that specific saved backup
are genuinely `Off` in the file (not a parsing gap -- our
recollection of that Combi's live state didn't match what was actually in
the saved backup), and Timbre 4's raw bytes (`number=85, bank=22`) decode
cleanly to `USER-F-085` once bank 22 was identified -- not the
`INT-A-093` recalled from memory. Both the external reference's own
independent test data and this project's byte-level analysis agree,
which is what settled it. Left in as an example of a "disagreement" that
turned out to be bad ground truth, not a model gap -- consistent with
this document's practice of recording what was *actually* resolved and
how, not just the final answer.

## 7. Notes from an external reference (not yet used by this parser)

[`docs/references/PCG-Structure-Kronos-DaBlick.txt`](https://github.com/jens-goes-mad/DIY-KORG-KRONOS-EDITOR/blob/main/docs/references/PCG-Structure-Kronos-DaBlick.txt)
(see [docs/references](https://github.com/jens-goes-mad/DIY-KORG-KRONOS-EDITOR/blob/main/docs/references/README.md)
for origin/license) goes further than this project has in a few areas.
Recorded here for later, even though nothing below is wired into
`PcgFile.cpp` yet:

- **`DIV1` chunk** (a `PCG1` sibling, right after the 16-byte file header):
  a table of counts/bitmasks for how many banks of each kind the file
  actually has (Program, Combi, Drum Kit, Wave Sequence, Global, DPI, Set
  List slots) -- this project currently discovers banks by scanning for
  chunk tags rather than reading this header, so it's an alternative
  (unused) source of the same information, not a gap in what currently
  works.
- **Program bank count discrepancy**: the external doc's `DIV1` example
  reads `21` Program banks, but this project has only ever found/parsed
  20 `MBK1`/`PBK1` chunks in real files (§5.2). Unresolved which is
  right -- possibly a 21st bank this project's chunk scan is missing
  entirely, or a quirk of that specific example file.
- **`MBK1` = EXi bank, `PBK1` = HD-1 bank**: the two Program bank tags
  this project already treats identically for name lookup (§5) turn out
  to signal which *sound engine* that bank's Programs use (EXi = the
  software synth engines like AL-1/MOD-7/etc., HD-1 = the PCM sample
  playback engine). Plausibly relevant to why some Combi Timbre blocks
  (§6) have visibly different internal parameter layouts from each other
  -- likely engine-dependent -- but not confirmed or acted on yet. The
  underlying *behavior* this describes -- engine assignment is a global,
  per-bank setting, never mixed within a bank -- **is** independently
  confirmed directly (§5.2); only the specific claim that the `MBK1`/
  `PBK1` tag is what encodes it remains externally sourced only.
- **A possible third Set List slot type -- bit width CONFIRMED 2026-08-08,
  a real Song-type slot still not reproduced**: this project's SBK1
  parsing (§4.3) used to read a single bit (`isProgram`: 1=Program,
  0=Combi). This external doc's claim of a byte with three possible
  values (`00=Combi, 01=Program, 02=Song`) is now independently confirmed
  by a *second*, separate source -- Korg's own SysEx parameter
  documentation (`docs/external/KORG/SetList.txt`) -- and this project's
  own decoder has been corrected to match (§4.3). What's still open: no
  real file has been seen with a slot actually carrying type value 2, so
  whether Set List slots genuinely *can* reference a Song/sequence
  directly (vs. that value simply being reserved/unused in practice) isn't
  confirmed, just the bit layout that would represent it if it exists.
- **`DKT1` (Drum Kits) / `WSQ1` (Wave Sequences)**: confirmed to contain
  `DBK1`/`WBK1` sub-bank chunks following the same general
  numRecords/bytesPerRecord header shape as every other bank type in this
  format (§5's now-corrected 2-field, 8-byte version, not the original
  3-field guess -- see §5's own correction note; not independently
  re-verified for DKT1/WSQ1 specifically, but the pattern has now held for
  every other chunk type checked) -- still entirely unparsed by this
  project (open question §8.5), but now known to at least share the
  familiar shape rather than being a total unknown. Unlike Programs/Combis' uniform
  128-slots-per-bank, the external doc's example shows **non-uniform**
  bank sizes here: Drum Kits split as 40 (Int) + 16 per USER letter
  (`000-039` Int, `040-055` U-A, ... up to `136-151` U-G, 152 total);
  Wave Sequences as 150 (Int) + 32 per USER letter (`000-149` Int,
  `150-181` U-A, ... up to `342-373` U-G). Doesn't affect this
  project's existing bank-scanning code either way -- it already reads
  each bank's own `numRecords` from its header rather than assuming 128
  -- just recorded since it's a real structural difference from every
  bank type parsed so far.
- **An `INI1` chunk tag**, seen once in the external doc's example
  (immediately after `GLB1`, before what looks like a second
  `SLS1`/`PRG1`/`MBK1`/`PBK1` sequence starting right after it) --
  purpose entirely unknown, not observed by this project's own chunk
  scan yet. Whether that apparent second Set-List/Program sequence is a
  real second copy of something (an `.SNG` "song snapshot" bundling its
  own referenced Set List/Programs alongside the main `.PCG` content,
  maybe?) or just how the source document orders its own notes isn't
  clear from the excerpt available -- flagged as a real "huh, what's
  that" rather than asserted as a confirmed structure.
- **An unresolved anomaly in the source itself**: its own test data shows
  a Timbre meant to reference `GM127` decoding to `number=126, bank=6`
  instead -- flagged by that document's own author as unexplained. Left
  unresolved there too; recorded here in case it becomes relevant once
  GM/bank-6 territory is explored further.

## 8. Open questions (consolidated)

1. **RESOLVED (2026-08-08), see §1.2**: the 4-byte field is not an
   ambiguous prefix *before* the tag -- it's a fixed 12-byte header's third
   word (`tag`, `size`, `dwX`), always right after `size` and right before
   content. Its own *meaning* is still a completely open question (a
   running byte offset? An index? Untested) -- only its position and
   fixed width are confirmed now, not what it contains.
2. **RESOLVED (2026-08-08)**: there is no separate `used`/`count` field --
   see §3.1/§4.1/§5's corrected header shapes. It was a misreading of an
   unrelated chunk-level field (`dwX`, open question #1) that bled into
   this project's understanding of SDB1/SBK1/PBK1/MBK1/CBK1's own content,
   caused by a bug in chunk-level parsing that happened to cancel out
   end-to-end until that bug was fixed on its own.
3. Byte +17's bits 0-2 (§4.3) -- **still open**, though bit 3 is now
   resolved (see §4.3: Keyboard Track, bits 3-0). Korg's own SysEx
   parameter documentation (`docs/external/KORG/SetList.txt`) plus a
   direct byte-level check confirm bits 3-0 of this byte are Keyboard
   Track (4 bits, `00~0F` = track 1-16) -- an exact bit-width match for
   what was flagged unexplained here, consistent with real files showing
   isolated non-zero values there independent of Font size/Transpose (e.g.
   `0x08`). Not yet wired into `SlotParams`/decoded anywhere in the app
   (nothing needs it yet), and the remaining bits 0-2 of this same byte are
   still genuinely unaccounted for.
4. Exactly which of the 20 PRG1 banks maps to which *display label* -- the
   lookup mechanism itself is confirmed (§5.3), and §5.2's label order is
   now fully confirmed for all 20 indices (2026-08-11). See #13 below for a
   more fundamental version of the same uncertainty.
5. `DKT1` (Drum Kits), `WSQ1` (Wave Sequences), `GLB1`, `DPI1`, and `INI1`
   (§7, tag observed once, never by this project directly) -- entirely
   unexplored. Unknown whether Set List slots can reference these
   directly (if so, the instrument-name lookup has a gap there too).
6. The older SoundQuest `.SQS` backup dialect (`LIST`/`FORM`/`BANK`
   wrapping, seen in some third-party backup tools) is structurally
   different from the `KORG`/`PCG1` dialect this document/parser covers --
   never tested against it, likely needs its own separate reverse-
   engineering pass if ever needed.
7. Reported (not yet reproduced): leading spaces disappearing from
   Comment text somewhere in a round-trip through the app. Neither the
   read nor write path does any trimming in code, so the cause -- if
   real -- isn't obvious from inspection alone.
8. **FULLY RESOLVED (2026-08-14)**: all 20 Program bank indices now have a
   confirmed raw Combi Timbre code (§6.2) -- `INT-E`=4, `USER-E`=21, both
   exactly the "obvious" extrapolation from their neighbors, after a brief
   2026-08-11 misreading had them swapped (`USER-E` reported as code 4, an
   apparent "genuine surprise" that turned out to just be a mistaken
   transcription of the real unit's display -- see §6.2's `INT-E`/`USER-E`
   entry for the full correction); `USER-FF`=29, the last gap, confirmed
   the same day and exactly matching the `+11`-offset pattern this time.
   This entire table (§6.2's `kConfirmedTimbreBanks`) is now complete --
   no remaining Combi Timbre bank codes to identify.
9. **Bit layout RESOLVED (2026-08-08)**, real occurrence still unconfirmed:
   the third SBK1 slot type ("Song", §7) is now correctly readable (Type
   is 2 bits, not the 1 this project originally decoded) -- see §4.3. No
   real file has been seen with a Song-type slot yet, so whether this
   value actually occurs in practice remains open.
10. Whether the external reference's `21`-Program-banks `DIV1` reading
    (§7) points at a real 21st bank this project's chunk scan is missing.
11. The file-header checksum flag (§1.1, byte offset 8) -- our real
    sample reads `0x01` ("checksum present" per the external reference),
    but where any such checksum would actually live, and over what range
    of bytes, hasn't been investigated at all.
12. The apparent second `SLS1`/`PRG1`/`MBK1`/`PBK1` sequence right after
    an `INI1` chunk in the external reference's example (§7) -- a real
    second copy of something, or an artifact of how that document's own
    notes are ordered? Not investigated against a real file.
13. **A more fundamental version of #4, surfaced 2026-08-07**: every
    Program/Combi bank "index" this project uses anywhere is actually just
    that bank's *position* among however many PRG1/CBK1 sub-bank chunks
    were found in the file, in file order -- not a confirmed bank identity
    tied to anything in the bytes themselves. This has been silently
    correct so far only because every real file examined happened to
    contain a complete, canonically-ordered set of banks. If a real backup
    can omit banks (plausible -- Kronos backup tools appear to let you
    choose which data to include when saving), a file missing one bank
    would silently relabel every later bank as one position earlier than
    its real identity, with nothing in this project currently able to
    detect it. Each PRG1/CBK1 sub-bank chunk's own first 4 bytes (see #2's
    `used`/`count` field -- currently read and discarded, "meaning not
    understood yet") are a real candidate for a per-chunk identity field
    that would fix this properly. Not yet investigated with real test data
    that's actually missing a known bank.
14. **Mostly RESOLVED (2026-08-13), see §5.5**: a 2-byte field (offset
    2632-2633) that differs between a factory "Init Program"'s bytes in
    different banks turned out to be "Tone Adjust"/"Switch8 On Value" (a
    real Program parameter, per Korg's own official reference), not a
    per-bank identity tag as first suspected. Still open: whether writing
    one bank's Init Program template into a different bank's slot (the
    planned Duplicates-panel feature) is safe given that value isn't
    identical across banks even for untouched factory content.

## 9. Where this is implemented

- [`src/kronos/PcgFile.{h,cpp}`](https://github.com/jens-goes-mad/DIY-KORG-KRONOS-EDITOR/tree/main/src/kronos) --
  the parser itself: chunk-tag scanning, SDB1/SBK1/CBK1/MBK1/PBK1 record
  parsing, the instrument-name cross-reference.
- [`src/bridge/EditorBridge.{h,cpp}`](https://github.com/jens-goes-mad/DIY-KORG-KRONOS-EDITOR/tree/main/src/bridge) --
  exposes parsed data (and edits: Set List move/copy, Program copy, and Set
  List slot Comment/Color/Volume -- the latter three via
  `getSongRecordBytes`/`putSongRecordBytes`, writing straight into the
  loaded file's own raw bytes) to the web UI, plus `saveFileAs()` to write a
  dataset's edited bytes back to a file (`PcgFile::save()`, a verbatim write
  of the retained buffer -- no Save UI wired up yet, see `STATE.md`).
- [`tools/generate_setlist_test_matrix.{js,cpp}`](https://github.com/jens-goes-mad/DIY-KORG-KRONOS-EDITOR/tree/main/tools) --
  hardware-validation helpers, not part of the shipped app: generate a
  matrix of Setlist Comment/Color/Volume/Font-size test permutations into a
  scratch file, for checking against a real Kronos by eye.
- [`resources/Init-Program-HD1.raw`/`Init-Program-EXi.raw`](https://github.com/jens-goes-mad/DIY-KORG-KRONOS-EDITOR/tree/main/resources) --
  real, extracted Korg factory "Init Program"/"Init EXi Program" record
  bytes (see §5.5), meant as this app's own known-good template for a
  future "clear a Program slot" feature. Not read by the app yet.
- See the top-level [`README.md`](https://github.com/jens-goes-mad/DIY-KORG-KRONOS-EDITOR/blob/main/README.md)
  for how to build/run the app, and
  [`STATE.md`](https://github.com/jens-goes-mad/DIY-KORG-KRONOS-EDITOR/blob/main/STATE.md)
  for current project status and the same open questions in project-planning form.
