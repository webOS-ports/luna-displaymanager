#!/bin/sh
# A && pass || bad is safe here: pass and bad always succeed.
# shellcheck disable=SC2015
# Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
#
# SPDX-License-Identifier: Apache-2.0
#
# run-on-device.sh [iterations]
#
# Run on the device, as root, against the installed LunaDisplayManager. It
# changes the timeout and the brightness and puts both back when it ends
# (however it ends), holds and drops a
# display block, restarts the daemon once (watching the bus while it starts),
# and sends every method payloads it does not expect. It fails if the daemon
# restarts on its own, answers wrongly, or keeps growing while called in a
# loop (iterations, default 500).
#
# Needs luna-send and ls-monitor (both in the image) and busybox.
set -u

N=${1:-500}
DM=luna://com.palm.display
fail=0
pass() { echo "PASS $*"; }
bad() { echo "FAIL $*"; fail=1; }

send() { luna-send -n 1 "$DM/$1" "$2" 2>&1; }
field() { sed -n "s/.*\"$1\": *\([^,} ]*\).*/\1/p" | head -n 1; }
pid() { systemctl show -p MainPID --value luna-displaymanager; }
rss() { awk '/VmRSS/ { print $2 }' "/proc/$(pid)/status"; }

systemctl is-active -q luna-displaymanager || { echo "luna-displaymanager is not running"; exit 2; }

# The timeout and the brightness are stored, and the hostile payloads below
# change them too ({"maximumBrightness":-2147483648} is clamped to 1, and
# kept), so both are read first and put back however the script ends.
r=$(send control/getProperty '{"properties":["maximumBrightness","timeout"]}')
old_timeout=$(echo "$r" | field timeout)
old_brightness=$(echo "$r" | field maximumBrightness)
case "$old_timeout$old_brightness" in
	''|*[!0-9]*) echo "cannot read the current settings: $r"; exit 2 ;;
esac
# shellcheck disable=SC2329  # called from the EXIT trap
restore() {
	send control/setProperty "{\"timeout\":$old_timeout,\"maximumBrightness\":$old_brightness}" >/dev/null
	r=$(send control/getProperty '{"properties":["maximumBrightness","timeout"]}')
	if [ "$(echo "$r" | field timeout)" = "$old_timeout" ] \
	   && [ "$(echo "$r" | field maximumBrightness)" = "$old_brightness" ]; then
		echo "restored timeout $old_timeout, maximumBrightness $old_brightness"
	else
		echo "FAIL could not restore timeout $old_timeout, maximumBrightness $old_brightness: $r"
		exit 1
	fi
}
trap restore EXIT
trap 'exit 1' INT TERM

# ---- the power signals at start-up ------------------------------------------
# The daemon asks batteryd for the charger and battery state once, and never
# broadcasts an empty USBDockStatus or asks telephony for the charge source.
mon=/tmp/dm-monitor.$$
ls-monitor >"$mon" 2>&1 &
mpid=$!
sleep 2
systemctl restart luna-displaymanager
sleep 10
kill "$mpid" 2>/dev/null
wait "$mpid" 2>/dev/null
own=$(grep -E "TX +signal.*com\.palm\.display " "$mon")
q=$(printf '%s\n' "$own" | grep -c "/com/palm/power/chargerStatusQuery")
[ "$q" = 1 ] && pass "one chargerStatusQuery at start-up" || bad "chargerStatusQuery sent $q times"
q=$(printf '%s\n' "$own" | grep -c "/com/palm/power/batteryStatusQuery")
[ "$q" = 1 ] && pass "one batteryStatusQuery at start-up" || bad "batteryStatusQuery sent $q times"
printf '%s\n' "$own" | grep -q "/com/palm/power/USBDockStatus" \
	&& bad "the display manager broadcast a USBDockStatus" || pass "no USBDockStatus from the display manager"
grep -q "com.palm.display.*chargeSourceQuery" "$mon" && bad "chargeSourceQuery called" || pass "no chargeSourceQuery"
if grep -q "com.palm.display.*com.palm.location.*getCurrentPosition" "$mon"; then
	bad "getCurrentPosition called"
else
	pass "no getCurrentPosition"
fi
if grep -q "RX signal.*com.palm.display .*/com/palm/power/chargerStatus\b\|RX signal.*com.palm.display .*/com/palm/power/USBDockStatus" "$mon"; then
	pass "batteryd's answer reached the display manager"
else
	bad "no charger status reached the display manager"
fi
rm -f "$mon"
sleep 2
start_pid=$(pid)

# ---- properties --------------------------------------------------------------
r=$(send control/getProperty '{"properties":["maximumBrightness","timeout","requestBlock","onWhenConnected","powerKeyBlock","proximityEnabled"]}')
echo "$r" | grep -q '"returnValue": *true' && pass "getProperty" || bad "getProperty: $r"

r=$(send control/setProperty '{"timeout":45}')
t=$(send control/getProperty '{"properties":["timeout"]}' | field timeout)
[ "$t" = 45 ] && pass "timeout round trip" || bad "timeout: set 45, read $t ($r)"

r=$(send control/setProperty '{"timeout":2147483647}')
t=$(send control/getProperty '{"properties":["timeout"]}' | field timeout)
echo "$r" | grep -q '"returnValue": *true' && [ "$t" = 2147483 ] \
	&& pass "too long a timeout is the longest" || bad "timeout 2147483647: $r, now $t"
send control/setProperty '{"timeout":45}' >/dev/null
r=$(send control/setProperty '{"timeout":"x"}')
t=$(send control/getProperty '{"properties":["timeout"]}' | field timeout)
echo "$r" | grep -q '"returnValue": *false' && [ "$t" = 45 ] \
	&& pass "timeout that is not a number refused" || bad "timeout \"x\": $r, now $t"
send control/setProperty "{\"timeout\":$old_timeout}" >/dev/null

b=$((old_brightness == 55 ? 56 : 55))
send control/setProperty "{\"maximumBrightness\":$b}" >/dev/null
v=$(send control/getProperty '{"properties":["maximumBrightness"]}' | field maximumBrightness)
[ "$v" = "$b" ] && pass "maximumBrightness round trip" || bad "maximumBrightness: set $b, read $v"
send control/setProperty '{"maximumBrightness":1000}' >/dev/null
v=$(send control/getProperty '{"properties":["maximumBrightness"]}' | field maximumBrightness)
[ "$v" = 100 ] && pass "maximumBrightness clamped" || bad "maximumBrightness 1000 read back as $v"
send control/setProperty '{"maximumBrightness":-5}' >/dev/null
v=$(send control/getProperty '{"properties":["maximumBrightness"]}' | field maximumBrightness)
[ "$v" = 1 ] && pass "negative maximumBrightness is the minimum" || bad "maximumBrightness -5 read back as $v"
r=$(send control/setProperty '{"maximumBrightness":"bright"}')
v=$(send control/getProperty '{"properties":["maximumBrightness"]}' | field maximumBrightness)
echo "$r" | grep -q '"returnValue": *false' && [ "$v" = 1 ] \
	&& pass "maximumBrightness that is not a number refused" || bad "maximumBrightness \"bright\": $r, now $v"
send control/setProperty "{\"maximumBrightness\":$old_brightness}" >/dev/null

# A block lasts as long as the caller; luna-send -n 1 leaves at once.
r=$(send control/setProperty '{"requestBlock":true,"client":"dm-device-test"}')
sleep 1
v=$(send control/getProperty '{"properties":["requestBlock"]}' | field requestBlock)
echo "$r" | grep -q '"returnValue": *true' && [ "$v" = false ] \
	&& pass "requestBlock released when the client goes" || bad "requestBlock: $r, still held: $v"

r=$(send control/setProperty '[]')
echo "$r" | grep -q '"returnValue": *false' && pass "setProperty refuses a non-object" || bad "setProperty []: $r"

# ---- hostile payloads --------------------------------------------------------
for m in status control/status control/getProperty control/setProperty control/setState \
         control/lockStatus control/alert control/notifyUserActivity; do
	for p in '[]' 'null' '42' '"x"' '{}' '{"state":null}' '{"state":42}' '{"properties":null}' \
	         '{"properties":[null,1,{}]}' '{"requestBlock":true,"client":null}' '{"timeout":"60"}' \
	         '{"maximumBrightness":-2147483648}' '{"status":null}' '{"client":"%s%s%n"}'; do
		send "$m" "$p" >/dev/null
	done
done
[ "$(pid)" = "$start_pid" ] && pass "survived hostile payloads" || bad "daemon restarted (pid $start_pid -> $(pid))"

# ---- growth ------------------------------------------------------------------
# getProperty leaked its request on every call; the card shell's brightness
# slider calls it.
for _ in $(seq 1 50); do send control/getProperty '{"properties":["timeout"]}' >/dev/null; done
before=$(rss)
for _ in $(seq 1 "$N"); do
	send control/getProperty '{"properties":["maximumBrightness","timeout","onWhenConnected"]}' >/dev/null
done
after=$(rss)
grow=$((after - before))
# A leak of ~1 KiB per call shows as hundreds of KiB; allocator noise does not.
[ "$grow" -lt 128 ] && pass "RSS steady over $N getProperty calls (${before} -> ${after} KiB)" \
	|| bad "RSS grew ${grow} KiB over $N getProperty calls (${before} -> ${after} KiB)"

[ "$(pid)" = "$start_pid" ] && pass "no restart during the run" || bad "daemon restarted during the run"
exit $fail
