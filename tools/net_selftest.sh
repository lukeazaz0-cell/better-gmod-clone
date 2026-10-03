#!/usr/bin/env bash
# Runs a host and a client against each other (headless if xvfb-run is available).
set -u
BIN="${1:-./build/gmodclone}"
OUT="${2:-.}"
PORT="${PORT:-27999}"
RUN=(); command -v xvfb-run >/dev/null && RUN=(xvfb-run -a -s "-screen 0 960x540x24")
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
"${RUN[@]}" "$BIN" --width 960 --height 540 --name Host --host "$PORT" --autotest-host --shots "$OUT" > "$OUT/net_host.log" 2>&1 &
HOST=$!
sleep 5
"${RUN[@]}" "$BIN" --width 960 --height 540 --name Client --connect "127.0.0.1:$PORT" --autotest-client --shots "$OUT" > "$OUT/net_client.log" 2>&1
C=$?
wait $HOST
H=$?
grep -h "netest" "$OUT/net_host.log" "$OUT/net_client.log"
echo "host exit $H, client exit $C"
[ $H -eq 0 ] && [ $C -eq 0 ]
