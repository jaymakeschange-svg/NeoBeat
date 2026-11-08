#!/bin/bash
# Renders the sound-fix preview set: build 1 vs new voicing (loudness matched).
set -e
cd "$(dirname "$0")/.."
NEW=./build/neobeat_cli
OLD=./build/neobeat_cli_build1
R4="/mnt/user-data/uploads/Documents/Codex/2026-10-26/sen/outputs/Liquid-Swing-Source-and-Records/Liquid Swing 4 - Round 4 Clank"
W=out/preview_raw; rm -rf $W; mkdir -p $W/series $W/kits $W/presets
# 1. velocity series (five hits, rising) -- the way Round 4 was auditioned
for s in A0_round3_core A1_buoyant_core A5_bounce B1_core B4_liquid B6_drop C1_core C7_gong; do
  NB_CHAR=0 NB_GLUE=0 $NEW series round4/$s $W/series/${s}_BUILD1.wav >/dev/null
  $NEW series round4/$s $W/series/${s}_NEW.wav >/dev/null
done
cp "$R4/A0_clank_round3_core.wav" $W/series/A0_round3_core_ROUND4_ORIGINAL.wav
cp "$R4/A1_clank_buoyant_core.wav" $W/series/A1_buoyant_core_ROUND4_ORIGINAL.wav
cp "$R4/A5_clank_bounce.wav" $W/series/A5_bounce_ROUND4_ORIGINAL.wav
for s in R5_02_rubber_drumhead_kick R5_08_rubber_snare R5_03_inner_tube_bonk R5_05_junkyard_clank; do
  NB_CHAR=0 NB_GLUE=0 $NEW series round5/$s $W/series/${s}_BUILD1.wav >/dev/null
  $NEW series round5/$s $W/series/${s}_NEW.wav >/dev/null
done
# 2. kit tours
for k in 0 1 2; do
  $OLD kittour $k $W/kits/kit$((k+1))_BUILD1.wav >/dev/null
  $NEW kittour $k $W/kits/kit$((k+1))_NEW.wav >/dev/null
done
# 3. presets
for p in 0 10 20 32 40; do
  $OLD preset $p $W/presets/p${p}_kit1_BUILD1.wav 4 0 >/dev/null
  $NEW preset $p $W/presets/p${p}_kit1_NEW.wav 4 0 >/dev/null
done
$OLD preset 26 $W/presets/p26_kit2_BUILD1.wav 4 1 >/dev/null; $NEW preset 26 $W/presets/p26_kit2_NEW.wav 4 1 >/dev/null
$OLD preset 43 $W/presets/p43_kit3_BUILD1.wav 4 2 >/dev/null; $NEW preset 43 $W/presets/p43_kit3_NEW.wav 4 2 >/dev/null
echo rendered
