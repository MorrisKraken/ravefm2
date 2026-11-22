# RAVE FM 2

An early hardcore groovebox firmware for the M-VAVE FM-1.

- 6 tracks: kick, break, 909, bass, lead, stab/hoover (any track can take any machine)
- 108 hardcore kick presets (gabber, frenchcore, uptempo, doom...) with their own pitch, shape, distortion and envelope
- Sliced breakbeats, played in time
- A 909 kit, every piece on its own row
- Rave leads, hoovers, screeches and stabs
- 4 patterns, a song mode, melodies up to 128 steps, live recording (REC)
- Sidechain on every track, pumped by the kick
- Live FX on the keys (hold FX): riser (six sounds to mix), drops, rolls, filters, kill bass, snare bar, mutes, tape stop
- Projects saved with a name, colour themes, key lights, an animated character on every page

## Manual

Everything RAVE FM 2 does, with screenshots: **[docs/MANUAL.md](docs/MANUAL.md)** (also as a PDF:
[docs/RAVE-FM-2-Manual.pdf](docs/RAVE-FM-2-Manual.pdf)).

## Install

Take `RAVEFM2.fwsc` from the latest release and install it like any FM-1 firmware (the update loader is in the
package). Going back to M-VAVE's V15 or another firmware works the same way.

## Build

Windows, with JieLi's toolchain in `C:\JL\pi32`: double-click `COMPILER-RAVEFM2.bat`
(it runs `python tools\build_ravefm.py`, the result is `build\ravefm\RAVEFM2.fwsc`, the log `resultat-ravefm2.txt`).
The package's three JieLi SDK files are downloaded, or taken from M-VAVE's `FM-1_015.fwsc` with `--v15`.

PC simulator and tests: `sh tools/host/build.sh`, `python3 tools/host/tests/run_tests.py`.

## Licence

GPL-3.0-only (see `LICENSE` and `LICENSING.md`). RAVE FM 2 is built on the open FM-1 platform of Felucca
(Leo Kuroshita, Hügelton Instruments) and JIANT FM (Juan Jiant); their notices and the other parts' licences are
listed in `LICENSING.md`.
