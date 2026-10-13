#!/bin/sh
#
# netem_latency.sh - routertc Goal 4: inject latency and jitter via netem.
#
# Replicates the physical speed-of-light propagation delay between financial
# hubs. The canonical reference in the task spec is Hong Kong to Singapore,
# ~22.4 ms one-way plus 0.1 ms of random jitter:
#
#   tc qdisc add dev eth0 root netem delay 22.4ms 0.1ms
#
# In HFT backtests (HftBacktest etc.) this artificially skews packet timestamps
# and shows how execution slips when your apparent network is slower than the
# real colo path.
#
# IMPORTANT: this script REPLACES the root qdisc. Apply it on the WAN side
# (eth0) for a symmetric hub-to-hub delay, or on the LAN bridge (br-lan) to
# impair only one leg.
#
# Usage:
#   ./netem_latency.sh [interface] [delay] [jitter]
#
# Examples:
#   ./netem_latency.sh eth0 22.4ms 0.1ms     # HK -> SG
#   ./netem_latency.sh eth0 0.3ms 0.02ms     # dense metro hub
#   ./netem_latency.sh br-lan 5ms 1ms        # LAN-side jitter lab
#

set -e

IFACE="${1:-eth0}"
DELAY="${2:-22.4ms}"
JITTER="${3:-0.1ms}"

if ! command -v tc >/dev/null 2>&1; then
    echo "error: tc (iproute2) not found" >&2
    exit 1
fi
if [ "$(id -u)" -ne 0 ] && [ -z "${RTC_NO_ROOT_CHECK:-}" ]; then
    echo "error: run as root (or set RTC_NO_ROOT_CHECK=1)" >&2
    exit 1
fi

echo "== routertc netem latency injection =="
echo "interface : $IFACE"
echo "delay     : $DELAY"
echo "jitter    : $JITTER"
echo

tc qdisc del dev "$IFACE" root 2>/dev/null || true

tc qdisc add dev "$IFACE" root handle 1: netem delay "$DELAY" "$JITTER"

echo "installed qdisc:"
tc qdisc show dev "$IFACE"

echo
echo "measure the skew from a LAN client with:"
echo "  ping -n 100 $IFACE  (from a workstation on the LAN)"
echo "or run an HftBacktest replay and watch execution slip."
