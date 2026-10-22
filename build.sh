#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
CC=${ARM_CC:-arm-none-eabi-gcc}
OBJCOPY=${ARM_OBJCOPY:-arm-none-eabi-objcopy}
NM=${ARM_NM:-arm-none-eabi-nm}
# Hot DSP loops (tape_fast_m7.c, dsp_fast_m7.c) build at -O2; all other M7
# code stays at -Os. No fast-math: float semantics are unchanged.
M7_FLAGS="-mcpu=cortex-m7 -mfpu=fpv5-d16 -mthumb -mfloat-abi=hard -ffreestanding -nostdlib -fno-builtin -fno-unwind-tables -fno-asynchronous-unwind-tables -Wall -Wextra -Werror"
for core in 7 4; do
  if [ "$core" = 7 ]; then FPU=fpv5-d16; else FPU=fpv4-sp-d16; fi
  "$CC" -mcpu="cortex-m$core" -mfpu="$FPU" -mthumb -mfloat-abi=hard -Os \
    -ffreestanding -nostdlib -fno-builtin -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -Wall -Wextra -Werror -Wl,--build-id=none,-e,0 \
    -T "src/cm$core.ld" -o "build/cm$core.elf" "src/cm${core}_cpu.c"
  "$OBJCOPY" -O binary -j .cave "build/cm$core.elf" "build/cm$core.bin"
  "$NM" "build/cm$core.elf" > "build/cm$core.symbols"
done
# alpha.6 overdub fix: in-place PCM24 loops for the stock 0x081-page bodies.
"$OBJCOPY" -O binary -j .pcmhot build/cm4.elf build/pcmhot.bin
"$OBJCOPY" -O binary -j .pcmdm build/cm4.elf build/pcmdm.bin
"$OBJCOPY" -O binary -j .pcmds build/cm4.elf build/pcmds.bin
"$CC" $M7_FLAGS -Os -c -o build/gr7_main.o src/gr_m7.c
"$CC" $M7_FLAGS -O2 -fno-tree-loop-distribute-patterns -c -o build/gr7_fast.o src/tape_fast_m7.c
"$CC" $M7_FLAGS -Wl,--build-id=none,-e,0 -T src/gr7.ld \
  -o build/gr7.elf build/gr7_main.o build/gr7_fast.o
"$OBJCOPY" -O binary -j .cave build/gr7.elf build/gr7.bin
"$NM" build/gr7.elf > build/gr7.symbols
"$CC" $M7_FLAGS -Os -c -o build/delay7_main.o src/delay_m7.c
"$CC" $M7_FLAGS -O2 -fno-tree-loop-distribute-patterns -c -o build/delay7_fast.o src/dsp_fast_m7.c
"$CC" $M7_FLAGS -Wl,--build-id=none,-e,0 -T src/delay7.ld \
  -o build/delay7.elf build/delay7_main.o build/delay7_fast.o
"$OBJCOPY" -O binary -j .cave build/delay7.elf build/delay7.bin
"$NM" build/delay7.elf > build/delay7.symbols
"$CC" -mcpu=cortex-m7 -mfpu=fpv5-d16 -mthumb -mfloat-abi=hard -Os \
  -ffreestanding -nostdlib -fno-builtin -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -Wall -Wextra -Werror -Wl,--build-id=none,-e,0 -T src/gate7.ld \
  -o build/gate7.elf src/gate_m7.c
"$OBJCOPY" -O binary -j .cave build/gate7.elf build/gate7.bin
"$NM" build/gate7.elf > build/gate7.symbols
rm -f build/gr7_main.o build/gr7_fast.o build/delay7_main.o build/delay7_fast.o
python3 tools/make_release.py "$@"
