---
title: Experimental
links:
  - title: Work in progress, not yet stable or fully documented
    description: currently, decoding the Kronos's Insert/Master/Total Effects and a much bigger parameter-editing UI to go with it
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
yet. Screenshots to follow once the UI itself settles down.

## Effects

The Kronos ships 185 distinct Insert/Master/Total Effect algorithms. Work is underway
to decode what each one's own parameters actually mean, following the same
ground-truth-first method as the rest of this project -- nothing here gets written up
as fact until it's checked against real data.

Alongside that, a much larger parameter-editing UI has been built to work with what
that decoding turns up -- real controls (knobs, toggles) plus generic tables for the
long lists of values a lot of this data comes in. Both are still moving targets.

This work lives in this project's own optional, private companion module (see the
main README for what that is and why) -- not required to build or run the app, and
not covered in detail here yet.
