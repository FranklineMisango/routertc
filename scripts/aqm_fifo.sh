#!/bin/sh
#
# aqm_fifo.sh - routertc Goal 2: Active Queue Management experiment.
#
# In HFT, router-side buffering is the enemy. During a market-data burst the
# qdisc buffers frames, so they arrive late and stale. The goal is to transmit
# or drop, never to queue. This script replaces the default qdisc on a LAN
# bridge with a bare-bones FIFO carrying an intentionally microscopic buffer.
#
# Target interface defaults to br-lan (OpenWrt bridge at 192.168.128.100/24)
# and can be overridden.
#
# Usage:
#   ./aqm_fifo.sh [interface] [limit]
#
# Examples:
#   ./aqm_fifo.sh br-lan 8        # 8-packet FIFO, brutal
#   ./aqm_fifo.sh eth0 32         # gentler 32-packet FIFO
#

set -e

IFACE="${1:-br-lan}"
LIMIT="${2:-8}"

# Sanity checks: require iproute2 (tc) and root.
if ! command -v tc >/dev/null 2>&1; then
    echo "error: tc (iproute2) not found" >&2
    exit 1
fi
if [ "$(id -u)" -ne 0 ] && [ -z "${RTC_NO_ROOT_CHECK:-}" ]; then
    echo "error: run as root (or set RTC_NO_ROOT_CHECK=1)" >&2
    exit 1
fi

echo "== routertc AQM experiment =="
echo "interface : $IFACE"
echo "fifo limit: $LIMIT packets"
echo

# Remove any existing root qdisc so the new one is clean.
tc qdisc del dev "$IFACE" root 2>/dev/null || true

# Strict FIFO (pfifo) with a tiny limit. No CAKE, no FQ-CoDel fairness -
# just a drain queue so small that bursts overflow into drops immediately.
tc qdisc add dev "$IFACE" root handle 1: pfifo limit "$LIMIT"

echo "installed qdisc:"
tc qdisc show dev "$IFACE"

echo
echo "observation: with a $LIMIT-packet buffer, a burst larger than $LIMIT"
echo "frames will cause instantaneous drops. Capture drop vs queued with:"
echo "  tc -s qdisc show dev $IFACE"
