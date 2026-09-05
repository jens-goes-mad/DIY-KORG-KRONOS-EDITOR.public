---
title: MIDI SysEx Protocol
links:
  - title: Korg's own Kronos MIDI System Exclusive protocol
    description: Excl Header, Mode Change, Object Dump Request/Dump, 7-bit/8-bit packing, and what's actually been verified so far
menu:
    main:
        weight: 6
        params:
            icon: broadcast

toc: true
---
This page is a different kind of reference from [the file format page](/format): that
page documents the `.PCG`/`.SNG` file *this project reverse-engineered from scratch*, byte
by byte. This page documents Korg's own MIDI System Exclusive (SysEx) *wire protocol* for
the Kronos -- the messages it sends and receives live over a MIDI cable, not the file it
writes to disk. Korg has published this protocol officially (a "MIDI Implementation"
document, Version 1.27, dated Oct 22 2016), so most of what's below is Korg's own
documented behavior, not something derived by hex-inspecting a file -- summarized here in
this project's own words, not reproduced verbatim.

**Where the source document lives**: kept in this project's private companion submodule
(see the repo's own `CLAUDE.md`/`STATE.md` for what that is and why), not redistributed in
this public repo -- unlike [the file format page](/format)'s own `docs/external/KORG/`
sources. This page exists specifically so the *protocol facts* are public even though the
implementation currently exercising them isn't yet.

Status legend, distinct from the file format page's (that page's ground truth is real
file bytes; this page's is Korg's own spec text plus a small amount of independent
testing):

- **per spec**: stated in Korg's own MIDI Implementation document -- not independently
  re-derived, but not a guess either; this is Korg's own official documentation of their
  own protocol.
- **loopback-verified**: this project actually built and sent these exact bytes over a
  real MIDI connection (a local IAC virtual bus, not a real Kronos) and confirmed the
  wire format, byte for byte, matches what's described below.
- **unverified against real hardware**: no physical Kronos has been connected to this
  project's own development environment yet -- what a REAL instrument actually replies
  with, for any message on this page, is still unknown.

---

## Excl Header

Every message starts the same way: `F0 42 3g 68`.

- `F0` -- start of a MIDI System Exclusive message (standard MIDI, not Korg-specific).
- `42` -- Korg's own manufacturer ID (*per spec*, standard MIDI convention -- not
  something this specific document itself explains, general Korg SysEx knowledge).
- `3g` -- a single byte: the fixed high nibble `3` (Korg's own format-code convention) and
  the instrument's own **Global MIDI Channel** (0-15) as the low nibble. This is the one
  parameter present in literally every message on this page.
- `68` -- the Kronos's own product ID (*per spec*).

Every message ends with `F7` (standard MIDI, End of Exclusive).

*loopback-verified*: sending a real Mode Change message with channel 0 produced
`F0 42 30 68 ...` -- `0x30 = 0x30 | 0`, confirming the channel-nibble merge above.

## Function codes this project has actually looked at

The full function code table in Korg's document covers dozens of messages (parameter
editing, KARMA control, sequencer transport, sample management, and more) that haven't
been examined here at all. The subset actually read closely so far, all around
reading/switching state rather than editing individual parameters:

| Func   | Name                          | Direction         |
|--------|-------------------------------|--------------------|
| `4E`   | Mode Change                   | Receive/Transmit  |
| `72`   | Object Dump Request           | Receive           |
| `73`   | Object Dump                   | Receive/Transmit  |
| `74`   | Current Object Dump Request   | Receive           |
| `75`   | Current Object Dump           | Receive/Transmit  |
| `76`   | Store Bank Request            | Receive/Transmit  |
| `77`   | Dump Bank Request             | Receive           |
| `37`   | Bank Digest Request           | Receive           |
| `38`   | Bank Digest                   | Transmit          |
| `24`   | Reply                         | Transmit          |

"Receive" is from the Kronos's own point of view -- something we (an editor/controller)
send *to* it; "Transmit" is something it sends back.

## Mode Change (func `4E`)

`F0 42 3g 68 4E [mode] F7` -- 7 bytes total. `[mode]` is one byte, `0000 mmmm` -- the high
nibble is always zero, `mmmm` is the mode:

| Value | Mode        |
|-------|-------------|
| 0     | Combination |
| 2     | Program     |
| 4     | Sequencer   |
| 6     | Sampling    |
| 7     | Global      |
| 8     | Disk        |
| 9     | Set List    |

(1, 3, 5 are reserved.) After receiving this message the instrument changes mode and
transmits a Reply (func `24`, see below); it also transmits this SAME message whenever the
mode changes by any other means, front-panel included -- so a message arriving on this
function code doesn't necessarily mean *we* caused it.

*loopback-verified*: sending `channel=0, mode=Set List (9)` produced exactly
`F0 42 30 68 4E 09 F7`.

## Object Dump Request (func `72`) / Object Dump (func `73`)

`F0 42 3g 68 72 [obj] [bank] [idxHi] [idxLo] F7` -- requests a dump of one specific object.
The instrument replies with either the dump itself (func `73`, same shape but func `72`
becomes `73` and the payload data follows) or a Reply (func `24`) with a non-zero error
code if the request itself was invalid (wrong bank type, target not found, etc. -- see
Reply below).

- `[obj]` -- what kind of object. The full table (*per spec*), most of it unexplored by
  this project beyond Program/Combination:

  | Value | Object                              |
  |-------|--------------------------------------|
  | `00`  | Program                              |
  | `01`  | Combination                          |
  | `02`  | Song Timbre Set                      |
  | `03`  | Global                               |
  | `04`  | Drum Kit                             |
  | `05`  | Wave Sequence                        |
  | `06`  | KARMA GE                             |
  | `07`  | KARMA Template                       |
  | `08`  | Song Control                         |
  | `09`  | Song Event *(disabled)*              |
  | `0A`  | Song Region                          |
  | `0C`  | KARMA GE RTP Info                    |
  | `0D`  | Set List                             |
  | `0E`  | Drum Track Pattern                   |
  | `0F`  | Drum Track Pattern Event             |
  | `10`  | Set List Slot Comments               |
  | `11`  | Set List Slot Name                   |
  | `12`  | Combi Name                           |
  | `13`  | Program Name                         |
  | `14`  | Song Name                            |
  | `15`  | Wave Sequence Name                   |
  | `16`  | Drum Kit Name                        |
  | `17`  | Set List Name                        |
  | `18`  | Song                                 |

- `[bank]` -- meaning depends on `[obj]` (*per spec*). For Program: `0`-`5` = INT-A..F,
  `10`-`1A` = GM/g(n) (read-only), `40`-`4D` = USER-A..G, AA..GG. For Combination:
  `0`-`6` = INT-A..G, `40`-`46` = USER-A..G. Several object types (Song, Set List, etc.)
  ignore `[bank]` entirely -- it must be `0`.
- `[idxHi]`/`[idxLo]` -- the object's index within the bank, split into two 7-bit MIDI
  bytes (bits 7-13 / bits 0-6 of a 14-bit value) -- standard MIDI practice for a value that
  can exceed 7 bits, since every SysEx data byte's own top bit must be 0.

*loopback-verified*: `channel=0, obj=Program(0), bank=INT-A(0), index=0` produced exactly
`F0 42 30 68 72 00 00 00 00 F7`, and the request correctly recognized its own echoed-back
request (over the loopback) does not itself count as a reply, discarding it and timing out
cleanly rather than misreading it as an answer.

## Store Bank Request (func `76`) / Dump Bank Request (func `77`)

Two related bulk operations, *per spec*, neither loopback-tested yet:

- **Store Bank Request** (`F0 42 3g 68 76 [obj] [bank] F7`) -- writes every Object Dump
  received for the given object/bank into permanent storage. Per the spec's own wording,
  data received via func `73` is held in a temporary buffer and NOT committed until a
  matching Store Bank Request arrives -- multiple dumps for the same bank can be sent
  first, then committed with one Store Bank Request.
- **Dump Bank Request** (`F0 42 3g 68 77 [obj] [bank] F7`) -- shorthand for requesting
  every object in a bank at once: triggers a SERIES of Object Dump (func `73`) messages,
  one per object, rather than one at a time via repeated func `72` requests. Most object
  types are supported; a few (disabled Song Event, KARMA GE RTP Info, Drum Track Pattern
  Event) are explicitly excluded, *per spec*.

## 7-bit / 8-bit data packing (relevant to any dump's own payload)

MIDI SysEx data bytes may never have their top bit set (that bit is reserved to mark
status bytes elsewhere in the MIDI stream), so an Object Dump's actual payload -- the
object's real, internal 8-bit data -- has to be repacked into 7-bit bytes for transit,
*per spec*: every 7 internal 8-bit bytes become 8 bytes on the wire -- one extra "high bit
collector" byte (holding the top bit of each of the following 7 bytes) followed by those 7
bytes with their own top bit stripped to 0.

```
sysExSize  = binarySize + (binarySize + 6) / 7
binarySize = (sysExSize / 8) * 7 + (sysExSize % 8 ? sysExSize % 8 - 1 : 0)
```

**Unverified**: whether an unpacked dump payload's bytes actually line up with this
project's own already-reverse-engineered `.PCG`/`.SNG` record layout ([the file format
page](/format)) is a genuinely open question -- the two were derived completely
independently (one from Korg's own SysEx spec, the other from hex-inspecting real backup
files) and nothing has checked whether they agree. Needs a real SysEx capture from an
actual Kronos to find out.

## Reply (func `24`)

`F0 42 3g 68 24 [code] F7` -- the instrument's generic acknowledgment/error response to
several message types above. `[code]`, *per spec*:

| Code | Meaning                                                        |
|------|-----------------------------------------------------------------|
| 0    | No error                                                         |
| 1    | Parameter type specified is incorrect for the current mode      |
| 2    | Unknown param message type, unknown parameter id or index       |
| 3    | Short or otherwise mangled message                              |
| 4    | Target object not found                                          |
| 5    | Insufficient resources to complete the request                  |
| 6    | Parameter value is out of range                                 |
| 7    | Internal error                                                   |
| 64   | Other error (e.g. wrong program bank type for the received dump)|
| 65   | Target object is protected                                      |
| 66   | Memory overflow                                                  |

Code 65 ("protected") lines up with a separate part of the spec (Mode Data's own Option
field) describing an independent protect flag per memory area -- Program, Combination,
Song, Drum Kit, Wave Sequence, KARMA GE, internal HDD save, and Set List each have their
own, *per spec*, not yet cross-checked against anything else.

## What's actually been built against this

This project's private companion module has a small, working MIDI transport (macOS/
CoreMIDI) that has sent real Mode Change and Object Dump Request messages over a local
MIDI loopback and confirmed the wire bytes above match exactly -- see the
loopback-verified notes throughout this page. What that transport looks like, and how
it's organized, isn't part of this public documentation (this project's private
companion module -- see `CLAUDE.md`/`STATE.md` -- covers MIDI SysEx transport work, kept
separate from this free/OSS repo's own from-scratch file-format reverse-engineering
scope). No physical Kronos has been connected to test any of this against real hardware
yet -- everything marked *unverified against real hardware* above stays that way until it
has.
