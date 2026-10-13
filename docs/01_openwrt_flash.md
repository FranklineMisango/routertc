# Goal 1 - Flash a Router with OpenWrt

The foundation of the whole project. Stock consumer router firmware refuses to
let you touch the kernel's Traffic Control (tc) subsystem or run raw apps.
OpenWrt hands you root SSH access to a full Linux on the box.

## What this buys you

- Direct control of the `tc` (Traffic Control) subsystem.
- Ability to load qdiscs (FIFO, netem, CAKE, fq\_codel) and inspect them.
- Root access to run `tcpdump`, `iptables`, `nftables`, and our DPDK relay.
- A reproducible environment inside UTM for the latency experiments.

## Choosing hardware

OpenWrt runs on a very long list of devices. For these latency experiments a
spare desktop PC with a multi-port Intel NIC is ideal (it also hosts the DPDK
work in Goal 3). Any 802.11ac/ax router with enough RAM (>=128 MB) will do for
the pure tc experiments.

## Reproducing this environment in UTM

Our router is a virtual machine under UTM (on macOS):

```
eth0    192.168.64.2/24    WAN (host NAT)
br-lan  192.168.128.100/24 LAN bridge the workstation will attach to
```

On the macOS host, the `Makefile` `install` target copies `scripts/`, `dpdk/`
and `docs/` into the router and marks the scripts executable:

```
make install RTC_ROUTER=root@192.168.128.100
```

## Quick verification on the router

After flashing and first boot, confirm you can reach tc and see the interfaces
from the task scenario:

```
root@OpenWrt:~# ip addr show
root@OpenWrt:~# tc qdisc show
```

You should see the default `fq_codel` root qdisc on `eth0` and `br-lan`. That
is the "standard kernel" baseline you will later replace.

## Next steps

Move on to `docs/02_aqm.md` to start replacing that default qdisc with an HFT
style strict FIFO.
