# RAVE FM 2 — work log (for whoever continues: decisions, state, next steps)

## Decisions (Maxime, 13 Nov 2026)
- Bare-metal firmware on the FM-1, without M-VAVE's SDK, on the open FM-1 platform (Felucca / JIANT, GPL-3.0; he is
  fine publishing the code). Nothing on screen says where it comes from; licences stay in the source.
- Boot screen: the original bat (assets in ../assets/logo, ../tools/gen_logo.py), in its violet, + "RAVE FM", nothing
  else. The rest of the UI: our own coherent palette (not JIANT's), black + acid yellow / lime family.
- Art: thin-line pixel drawings, one-colour on black, characters dancing between speakers (his two reference images:
  style only, our own characters), tied to the instruments and to hardcore / gamer / techno.
- Breaks: the three of today (AMEN, THINK, HOT PANTS). Bluetooth: none in v1 (BLE MIDI later).
- No intermediate installs: one complete delivery. He only double-clicks a .bat to compile.

## Base
- JIANT's released line V10.7.23 = commit 841f7dd90cf733c50942ae9292d2e2cb52619bd8 (web/STABLE of
  github.com/juanjiant-bit/JIANT). Its main branch (0.9.18) does NOT save on the newer flash chip: do not use it.
- Toolchain: JieLi's Windows toolchain on his PC (C:\JL\pi32: clang.exe, pi32v2-ld.exe, llvm-objcopy.exe,
  llvm-objdump.exe, llvm-link.exe). tools/build_win.py maps tool names; the package's SDK files are downloaded from
  JieLi's gitee or taken from M-VAVE's FM-1_015.fwsc (Documents\RAVEFM\ravefm_v3\sdk\firmware\). JIANT 0.9.18 built
  there in full (563,732 B app, all checks OK) on 13 Nov.
- Host checks: clang -fsyntax-only --target=i386-unknown-none -ffreestanding works on the firmware sources.

## Layout
- firmware/src/ravefm.c: the platform unit (HAL, libc, lcd, usb MIDI, TRS MIDI, flash, OTA update entry, board shim,
  main). In progress: rfm_board.c (fm1.h for RAVE FM's code: controls, LEDs, screen, MIDI, flash at an area of ours;
  the audio ISR calling tb_engine_render; MASTER applied there), rfm_main.c (V10's fm1_cstart as is: power init, rails,
  LZ4 unpack; then a simple main loop: wdt, usb_retry, bootguard, OCT-+OCT+ 5 s -> UBOOT, ota_service / ota_session,
  MIDI in -> fm1_app_midi_received, tb_app_tick).
- firmware/rfm/engine: RAVE FM's code (tb_*), firmware/rfm/gen: its generated data, firmware/rfm/include: fm1.h and the
  C headers. Flags: -DTB_LITE=0 -DTB_DANCE=0 -DTB_MEL_STEPS=128 -DTB_HAS_USER_BRK=0 -DTB_HAS_DLY_HIFI=0 -DTB_BRK3 -DRFM2
  (RAM then 292 KB: engine .bss must go to the pool, a section rename in the IR).
- Build (to write: tools/build_ravefm.py): each unit to IR, llvm-link, minsize for what no interrupt reaches, hot code
  in RAM (HOT_FNS: tb_engine_render), the sound first in the image (.text.a.*), our own linker script (V10's app.ld
  without .bt and the layout include), LZ4 packing of the RAM sections as V10's build_app does, fm1pkg_make.

## State (14 Nov)
- Platform unit done (src/ravefm.c, rfm_board.c, rfm_main.c), host syntax-clean. Own linker script firmware/rfm.ld,
  entry rfm_crt0.S. tools/build_ravefm.py: IR per unit, llvm-link, minsize off the ISR path, mul64 helpers (CPU
  erratum), ISR code first (.text.a.*), hot RAM code chosen by size, LZ4, checks, package. Full samples tried first,
  lighter (TB_SMP_LITE) if over the slot. No float anywhere; rfm_libc.c has the 64/32-bit division helpers.
- Screen (14 Nov, Maxime: "reprends JIANT"): JIANT's own renderer (firmware/rfm/jui/jgfx.c = JIANT's gfx.c) and its
  lens-page frame redone in rfm_screen.c (header, group tabs, 4 outlined cards, panel 84..223 sent by changed cells,
  footer with the track's steps); JIANT's faces (Chakra Petch S, Inter Tight M, Michroma L) with the values' figures in
  Chakra Petch SemiBold (gen_aa_font.py --preset ravefm); red/white/black palettes (tools/gen_rfm_palettes.py). The old
  pictures draw through rfm_gfx.h's calls on the panel. HOME: eight line-drawn ravers, one a track. FX held a while
  and let go untouched: LIVE FX locked open (JIANT's FX LOCK); FX again unlocks. Idle stage removed. rfm_cv/rfm_gfx.c
  moved to rfm/old/. Reference shots of JIANT: its web emulator's C built natively (scratch tool jshot.c).
- Presets: alphabetical inside each category (tb_app.c ord_*), lists unchanged.
- Host harness: tools/host/build.sh, rfm_shot (screenshots, GIFs).
- 14 Nov evening (Maxime's picks): 6 tracks (no 909 track: its data kept to the DROP's SD and CR, tools/slim_909.py);
  HOME = Maxime's two skeletons (assets/maxime/squelettes.jpg) rigged and dancing the hakken on the tempo (tools/gen_home.py,
  parts turned in the firmware), the left one on the kick, the right one when a melody plays too; a star field behind
  (warps on the notes); instruments = Maxime's line art (assets/maxime/planche1-2.png, tools/gen_art.py), one per kind
  of sound, lit/angry with the notes; knobs drawn on the cards (JIANT's ring); palettes of black + three colours
  (NEON ACIDE TOXIC FEU SPECTRE), the drawings in the same three inks; boot: the bat + RAVE FM in violet chrome
  (tools/gen_chrome_logo.py, lettered after the 90s wordmarks).
- First target build OK (14 Nov): app 483,416 B of the 584,336 B slot (100,920 free), full samples; pool spare 30,724 B;
  build/ravefm/RAVEFM2.fwsc 611,020 B. Not yet run on the FM-1. Two fixes the ROM check needed: no 0xFFC00000
  constants (the turns' masks; the synth filter guard now +-5 x 2^20).

## The flash budget (solved for now: 101 KB free, see above)
- App slot 584,336 B (XIP 0x02000120.., flash [0x4000, 0x93000)). RAVE FM's code + data measured on i386 -Os: ~530 KB
  with the FM-1's sample data (pi32 ~0.93 of i386) + the platform (~70 KB) => at the slot's limit before any new art.
- Options: (a) a data segment like V10's Bluetooth one (loader writes a package entry into the plain XIP window above
  0x93000; the app reads it in place) for the samples (breaks 109 KB, 909 ~100 KB, kicks ~50 KB): needs a loader change;
  (b) trims. Decide before writing the new UI.

## Next steps
1. rfm_board.c, rfm_main.c; syntax-check the platform unit on the host.
2. tools/build_ravefm.py + the linker script; a host simulator (from web/emu/felucca_web.c's approach) to run the whole
   firmware on the PC: screens, audio, storage.
3. Flash budget decision; then the new UI and art (sprites as GIFs first).
