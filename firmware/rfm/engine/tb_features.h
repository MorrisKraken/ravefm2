/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM: what this build holds. The full instrument by default; TB_LITE=1 (the M-VAVE FM-1, whose firmware leaves
 * ~70 KB of program and ~243 KB of RAM to the app) leaves out what the user chose to give up on 5 Nov 2026. */
#ifndef TB_FEATURES_H
#define TB_FEATURES_H
#ifndef TB_LITE
#define TB_LITE 0
#endif
#define TB_FULL (!TB_LITE)
#ifndef TB_HAS_CLIP
#define TB_HAS_CLIP TB_FULL         /* pattern copy / paste (the tools PCOPY, PPASTE) */
#endif
#ifndef TB_HAS_MUTATE
#define TB_HAS_MUTATE TB_FULL       /* MUTATIONS */
#endif
#ifndef TB_HAS_STACK
#define TB_HAS_STACK TB_FULL        /* the stacked effects (FLANGER, PHASER, TREMOLO on every track at once) */
#endif
#ifndef TB_HAS_USER_BRK
#define TB_HAS_USER_BRK TB_FULL     /* the user's own breaks (imported, kept in flash) */
#endif
#ifndef TB_HAS_MAN_SLICE
#define TB_HAS_MAN_SLICE TB_FULL    /* BREAK: slices set by hand */
#endif
#ifndef TB_HAS_TOOLS_GEN
#define TB_HAS_TOOLS_GEN TB_FULL    /* the tools EUCLID, RANDOM, HUMANIZE, REVERSE */
#endif
#ifndef TB_HAS_PLOCK
#define TB_HAS_PLOCK TB_FULL        /* parameter locks */
#endif
#ifndef TB_HAS_RATCHET
#define TB_HAS_RATCHET TB_FULL      /* a step's repeats */
#endif
#ifndef TB_HAS_CHANCE
#define TB_HAS_CHANCE TB_FULL       /* a step's chance */
#endif
#ifndef TB_HAS_SPEED
#define TB_HAS_SPEED TB_FULL        /* a track's speed */
#endif
#ifndef TB_HAS_SWING
#define TB_HAS_SWING TB_FULL
#endif
#ifndef TB_HAS_ROLL
#define TB_HAS_ROLL TB_FULL         /* the roll / repeat held on a key */
#endif
#ifndef TB_HAS_TAP
#define TB_HAS_TAP TB_FULL          /* tap tempo */
#endif
#ifndef TB_HAS_SAMPLE
#define TB_HAS_SAMPLE TB_FULL       /* the SAMPLE machine */
#endif
#ifndef TB_HAS_BRK_REV
#define TB_HAS_BRK_REV TB_FULL      /* a break backwards */
#endif
#ifndef TB_HAS_KICK_SW2
#define TB_HAS_KICK_SW2 TB_FULL     /* the kick's second sweep */
#endif
#ifndef TB_HAS_ACC909
#define TB_HAS_ACC909 TB_FULL       /* the 909's accents */
#endif
#ifndef TB_HAS_CHORUS
#define TB_HAS_CHORUS TB_FULL       /* each track's stereo chorus */
#endif
#ifndef TB_HAS_INS_MOD
#define TB_HAS_INS_MOD TB_FULL      /* the inserts CHORUS, FLANGER (their delay lines), TREMOLO, RING */
#endif
#ifndef TB_HAS_INS_LOFI
#define TB_HAS_INS_LOFI TB_FULL     /* the inserts LOFI, FOLD, WAH */
#endif
#ifndef TB_HAS_REV_LONG
#define TB_HAS_REV_LONG TB_FULL     /* the reverb's long tanks */
#endif
#ifndef TB_HAS_DLY_HIFI
#define TB_HAS_DLY_HIFI TB_FULL     /* the delay at full rate, 16 bits (else half rate, 8 bits) */
#endif
#ifndef TB_HAS_SPLASH
#define TB_HAS_SPLASH TB_FULL       /* the animated start */
#endif
#ifndef TB_HAS_THEMES
#define TB_HAS_THEMES TB_FULL       /* the colour themes */
#endif
#ifndef TB_HAS_HINTS
#define TB_HAS_HINTS TB_FULL        /* the help line */
#endif
#ifndef TB_HAS_HUD
#define TB_HAS_HUD TB_FULL          /* the big value as a knob turns */
#endif
#ifndef TB_HAS_WAVE
#define TB_HAS_WAVE TB_FULL         /* the break's waveform */
#endif
#ifndef TB_HAS_PICS
#define TB_HAS_PICS TB_FULL         /* the tracks' pages' pictures */
#endif
#ifndef TB_HAS_FONT_XL
#define TB_HAS_FONT_XL TB_FULL      /* the extra large font */
#endif
#ifndef TB_HAS_POLY_FULL
#define TB_HAS_POLY_FULL TB_FULL    /* every voice (else half) */
#endif
/* what the user may still give up for the FM-1 (each on unless built with -DTB_HAS_X=0: measured one by one) */
#define TB_OPT(x) x
#ifndef TB_HAS_SYNTH
#define TB_HAS_SYNTH 1              /* the SYNTH machine */
#endif
#ifndef TB_HAS_ACID
#define TB_HAS_ACID 1               /* the ACID machine (303) */
#endif
#ifndef TB_HAS_ARP
#define TB_HAS_ARP 1                /* the arpeggiator */
#endif
#ifndef TB_HAS_LFO
#define TB_HAS_LFO 1
#endif
#ifndef TB_HAS_SCALE
#define TB_HAS_SCALE 1              /* scales and chords */
#endif
#ifndef TB_HAS_DROPS
#define TB_HAS_DROPS 1              /* the DROP kinds besides the classic, their pages */
#endif
#ifndef TB_HAS_RISER
#define TB_HAS_RISER 1
#endif
#ifndef TB_HAS_LIVEFX
#define TB_HAS_LIVEFX 1             /* FX held + the keys: rolls, sweeps, kill bass... */
#endif
#ifndef TB_HAS_STYLE
#define TB_HAS_STYLE 1              /* the tool STYLE */
#endif
#ifndef TB_HAS_NOTES
#define TB_HAS_NOTES 1              /* SEQ's EDIT page */
#endif
#ifndef TB_HAS_TOOLS
#define TB_HAS_TOOLS 1              /* SEQ's tools page */
#endif
#ifndef TB_HAS_SONG
#define TB_HAS_SONG 1
#endif
#ifndef TB_HAS_REC
#define TB_HAS_REC 1                /* REC: played live into the steps */
#endif
#ifndef TB_HAS_MIDI
#define TB_HAS_MIDI 1               /* MIDI in (notes, clock) and out */
#endif
#ifndef TB_HAS_PHONE
#define TB_HAS_PHONE 1              /* the phone / page (SysEx) */
#endif
#ifndef TB_HAS_METRO
#define TB_HAS_METRO 1
#endif
#ifndef TB_HAS_MENU_PICS
#define TB_HAS_MENU_PICS 1          /* the menus' drawings (ENV, LFO, ARP, SCALE, FX, GLO curves) */
#endif
#ifndef TB_HAS_KICK_PAGE
#define TB_HAS_KICK_PAGE 1          /* the kick's page with its curve */
#endif
#ifndef TB_HAS_TEQ
#define TB_HAS_TEQ 1                /* each track's EQ */
#endif
#ifndef TB_HAS_MASTER
#define TB_HAS_MASTER 1             /* the master: EQ, glue comp, width, drive */
#endif
#ifndef TB_HAS_REVERB
#define TB_HAS_REVERB 1
#endif
#ifndef TB_HAS_DELAY
#define TB_HAS_DELAY 1
#endif
#ifndef TB_HAS_UNDO
#define TB_HAS_UNDO 1
#endif
#ifndef TB_HAS_LANES
#define TB_HAS_LANES 1              /* HOME's lanes as waveforms (else plain bars) */
#endif
#endif
