#!/bin/sh
# RAVE FM 2 on the PC (tools/host/rfm_shot.c): RAVE FM's code with the FM-1 build's flags
cd "$(dirname "$0")/../.."
E=firmware/rfm
gcc -O1 -g -w -std=gnu99 -DTB_LITE=0 -DTB_DANCE=0 -DTB_MEL_STEPS=128 -DTB_HAS_USER_BRK=1 -DTB_HAS_DLY_HIFI=0 -DTB_BRK3 -DRFM2 -DRFM_HOST -DTB_SMP_LITE \
    -I$E/engine -I$E/gen -iquote firmware/rfm/include -o build/rfm_shot tools/host/rfm_shot.c $(ls $E/engine/*.c | grep -v rfm_libc) -lm
