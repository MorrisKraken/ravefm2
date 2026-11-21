#!/bin/sh
# Audit 2: the binaries its tools run (combo.py, races.py, projfuzz.py)
cd "$(dirname "$0")/../../../.."
E=firmware/rfm
F="-g -w -std=gnu99 -DTB_LITE=0 -DTB_DANCE=0 -DTB_MEL_STEPS=128 -DTB_HAS_USER_BRK=1 -DTB_HAS_DLY_HIFI=0 -DTB_BRK3 -DRFM2 -DRFM_HOST -DTB_SMP_LITE -I$E/engine -I$E/gen -iquote $E/include"
ALL=$(ls $E/engine/*.c | grep -v rfm_libc)
ENG=$(ls $E/engine/*.c | grep -v "rfm_libc\|tb_app\|tb_store\|rfm_ui")
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
set -e
gcc -O2 $F -o build/combo tools/host/tests/audit2/combo.c $ENG -lm
gcc -O1 $F $SAN -o build/combo_asan tools/host/tests/audit2/combo.c $ENG -lm
gcc -O1 $F -DRFM_THREADS -fsanitize=thread -o build/rfm_tsan tools/host/rfm_shot.c $ALL -lm -lpthread
gcc -O1 $F -DRFM_THREADS $SAN -o build/rfm_tasan tools/host/rfm_shot.c $ALL -lm -lpthread
echo built
