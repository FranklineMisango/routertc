#!/bin/sh
#
# netem_reset.sh - routertc Goal 4: tear down any netem / qdisc experiment.
#
# Restores a plain FIFO root qdisc on the target interface so the router goes
# back to a clean forwarding state between experiments.
#
# Usage:
#   ./netem_reset.sh [interface]
#

set -e

IFACE="${1:-eth0}"

if ! command -v tc >/dev/null 2>&1; then
    echo "error: tc (iproute2) not found" >&2
    exit 1
fi
if [ "$(id -u)" -ne 0 ] && [ -z "${RTC_NO_ROOT_CHECK:-}" ]; then
    echo "error: run as root (or set RTC_NO_ROOT_CHECK=1)" >&2
    exit 1
fi

echo "== routertc: clearing qdiscs on $IFACE =="
tc qdisc del dev "$IFACE" root 2>/dev/null || echo "no root qdisc present"
echo "done"
tc qdisc show dev "$IFACE"
