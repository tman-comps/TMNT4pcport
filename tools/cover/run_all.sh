#!/bin/sh
# Coverage: plays every stage (god mode, fighting bot) and the menus/modes on the interpreter and
# writes one coverage log per run into $OUT (default build/cov). Then:
#   python3 -I tools/cdlmap.py roms/tmnt4.sfc analysis/tmnt4/map.json - build/cov
# usage: tools/cover/run_all.sh [frames_per_stage]   (needs `make tmnt_tool_interp`)
cd "$(dirname "$0")/../.."
OUT=${OUT:-build/cov}; mkdir -p $OUT
N=${1:-25000}
T=./tmnt_tool_interp; R=roms/tmnt4.sfc
BOOT="until:32:06:S until:72:05 w20"   # title -> 1 PLAYER -> turtle select ready ($32 = 6, $72 = 5)
TITLE="until:32:02 w400 pS w100"       # title menu shown, cursor on 1 PLAYER
GOD="god:on"
{
for s in 0 1 2 3 4 5 6 7 8 9; do
  t=$((s % 4)); mv=""; i=0; while [ $i -lt $t ]; do mv="$mv pR w20"; i=$((i+1)); done
  echo "st$s|$BOOT$mv poke:82:0$s pS w400 $GOD seed:$((s+1)) f$N"
done
# two players (pad 2 joins at turtle select), stages 1 and 6
echo "2p0|$BOOT p2S w60 pR w20 pS w20 p2S w400 $GOD seed:21 f$N"
echo "2p5|$BOOT p2S w60 poke:82:05 pR w20 pS w20 p2S w400 $GOD seed:22 f$N"
# no god mode: game over, continue screen, game over again
echo "over|$BOOT pS w400 seed:31 f6000 w600 pS w400 f6000 w3000"
# attract mode and demo playback
echo "attract|w20000"
# menus: 2 players, time trial, versus, options (cursor moves), back out
echo "menus|$TITLE pD w30 pS w300 pB w100 pB w300 $TITLE pD w30 pD w30 pS w100 pD w30 pR w20 pR w20 pD w30 pR w20 pD w30 pR w20 pL w20 pD w30 pD w30 pS w100 pB w100 pB w200 w3000"
echo "trial|$TITLE pR w30 pS w200 until:72:05 w20 pS w400 $GOD seed:41 f$N"
echo "versus|$TITLE pD w30 pR w30 pS w200 pS w100 p2S w100 pS w60 p2S w300 seed:51 r$N"
} | xargs -P ${JOBS:-4} -I{} sh -c 'n=$(echo "{}" | cut -d"|" -f1); s=$(echo "{}" | cut -d"|" -f2); '$T' '$R' interp "$s cdl:'$OUT'/$n.cdl shot:'$OUT'/$n.ppm save:'$OUT'/$n.state" > /dev/null 2>&1; echo "$n done"'
