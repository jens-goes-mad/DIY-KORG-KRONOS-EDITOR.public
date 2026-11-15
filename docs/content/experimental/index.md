---
title: Experimental
links:
  - title: Work in progress, not yet stable or fully documented
    description: effect and engine editors, a MIDI connection to the Kronos, and narrated how-to videos
menu:
    main:
        weight: 8
        params:
            icon: circuit-diode

comments: false
toc: true
---
This page is a heads-up, not a spec: things listed here are actively being worked on,
not yet finished, and not documented in byte-level detail anywhere else on this site
yet. Most of it lives in the app's optional, **private** companion module (see the main
README for what that is and why): it's built into the same app when present, but its
source isn't public, and the public app builds and runs fully without it.

## Effects

The Kronos ships 185 Insert/Master/Total Effect algorithms. Every one of them now has a
decoder for its parameters and its own editing panel -- real controls (knobs, switches,
pickers) laid out per effect, with a short description of every parameter taken from
Korg's Parameter Guide. All 185 panels have been checked once against that guide, which
caught and fixed over a hundred mislabeled controls; shared building blocks (envelope
source pickers, compressor sections, EQ bands) keep the panels consistent.

The **Insert Effect routing** -- which effect slot feeds which, and where the Combi's
Timbres go -- has its own table-style editor.

Following the project's ground-truth-first method, none of this is written up as fact
yet: the decoders still have to be checked against real Kronos data, parameter by
parameter.

## SGX-2 piano editor

A full parameter editor for the SGX-2 piano engine, in its own window, opened from a
Program row's engine-type button. SGX-2 is the smallest of the EXi engines, which made
it the right first one; the other engines have many times more parameters.

## MIDI connection to the Kronos

A MIDI SysEx connection to a real instrument: the app can request a Program,
Combination or Global dump from a connected Kronos and save it. macOS only for now, and
still a first pass -- one object at a time.

## How-to videos

The app can play back a scripted how-to: it clicks, types and points at things on its
own, shows short explanations, and records its own window while it does so. A separate
tool -- public, [DIY-AI-TOOLS](https://github.com/jens-goes-mad/DIY-AI-TOOLS.public) --
then voices each explanation with a local text-to-speech model and renders the finished
video: the recording pauses on each step while the narration plays, with the text shown
in an overlay box. One recording serves every language and voice.

The script player and the recorder are private; the how-to scripts for this app are too.
The first video, a tour of the main window, is in the works.
