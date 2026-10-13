#!/bin/sh
#
# bench_rtt.sh - routertc measurement helper.
#
# Measures round-trip latency (and jitter) toward a target host before, during,
# and after a tc experiment so you can quantify the effect of each qdisc.
#
# Reports min / avg / max / std-dev / packet-loss using ping, plus a coarse
# timestamp-delta estimate. For sub-microsecond tick-to-trade measurement you
# will want to use the DPDK relay's own counter and a PTP source on the NIC;
# this script is the quick human-readable sanity check.
#
# Usage:
#   ./bench_rtt.sh [host] [count] [interval_s]
#
# Examples (run from a LAN workstation against the UTM router):
#   ./bench_rtt.sh 192.168.128.100 200 0.2
#   ./bench_rtt.sh 192.168.64.2 500 0.1
#
# Defaults:
#   host=$RTC_BENCH_HOST or 192.168.128.100
#   count=100
#   interval=0.5
#

set -e

# Precedence: command-line $1, else $RTC_BENCH_HOST, else the default LAN bridge.
if [ -n "${1:-}" ]; then
    HOST="$1"
elif [ -n "${RTC_BENCH_HOST:-}" ]; then
    HOST="$RTC_BENCH_HOST"
else
    HOST="192.168.128.100"
fi
COUNT="${2:-100}"
INTERVAL="${3:-0.5}"

# Pick a ping syntax; BusyBox on OpenWrt is older than iputils, so probe.
if ping -h 2>&1 | grep -qi "busybox"; then
    PING_A="-c $COUNT -i $INTERVAL"
    PING_ARGS="$PING_A $HOST"
else
    PING_ARGS="-c $COUNT -i $INTERVAL $HOST"
fi

echo "== routertc RTT bench ==="
echo "target   : $HOST"
echo "count    : $COUNT"
echo "interval : ${INTERVAL}s"
echo

# -W is unsupported identically across BusyBox/iputils; keep it simple.
ping $PING_ARGS 2>&1 | tee /tmp/routertc_bench_rtt.log 2>/dev/null || true

echo
echo "summarised from /tmp/routertc_bench_rtt.log above."
