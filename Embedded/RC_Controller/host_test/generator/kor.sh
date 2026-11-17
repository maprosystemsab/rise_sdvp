#!/bin/sh
# Provkör elverkets förlopp (generator.c) på datorn mot en enkel modell av
# Honda + Goldenmotor + Maxim+. Ingen hårdvara behövs.  ./kor.sh
set -e
cd "$(dirname "$0")"
T=$(mktemp -d)
cp stub/*.h sim.c "$T"/
cp ../../generator.c ../../generator.h "$T"/
gcc -std=gnu99 -Wall -Wextra -I"$T" -o "$T/sim" "$T/sim.c" -lm
"$T/sim"
rm -rf "$T"
