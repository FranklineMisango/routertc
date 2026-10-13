#!/bin/sh
#
# sqm_experiment.sh - routertc Goal 2: SQM baseline for comparison.
#
# This is the COUNTER-EXPERIMENT to aqm_fifo.sh. SQM (CAKE / FQ-CoDel) tries
# to be 'fair' to all flows. Run this, measure latency/drops, then run
# aqm_fifo.sh and compare. The HFT takeaway is that fairness algorithms smooth
# bursts into queueing delay, which is exactly what a latency arbitrageur
# cannot tolerate.
#
# Requires OpenWrt package: luci-app-sqm or the raw 'cake' qdisc in the kernel.
# OpenWrt userland helper is sqm-scripts; here we drive tc directly.
#
# Usage:
#   ./sqm_experiment.sh [interface] [r2q] [limit]
#
# Defaults: br-lan, r2q=8, limit=1000 packets.
#

set -e

IFACE="${1:-br-lan}"
R2Q="${2:-8}"
LIMIT="${3:-1000}"

if ! command -v tc >/dev/null 2>&1; then
    echo "error: tc (iproute2) not found" >&2
    exit 1
fi
if [ "$(id -u)" -ne 0 ] && [ -z "${RTC_NO_ROOT_CHECK:-}" ]; then
    echo "error: run as root (or set RTC_NO_ROOT_CHECK=1)" >&2
    exit 1
fi

echo "== routertc SQM baseline (CAKE) =="
echo "interface : $IFACE"
echo

tc qdisc del dev "$IFACE" root 2>/dev/null || true

# CAKE: bandwidth is auto-probed by default; we keep it that way so the only
# knob we tweak is the per-flow 'fairness' behaviour itself.
tc qdisc add dev "$IFACE" root handle 1: cake bandwidth unlimited r2q "$R2Q" \
    overhead 22 limit "$LIMIT"

echo "installed qdisc:"
tc qdisc show dev "$IFACE"

echo
echo "compare: run  tc -s qdisc show dev $IFACE  under load and note how"
echo "CAKE queues burst traffic instead of dropping it at the line rate."
