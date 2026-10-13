# routertc

Low level manipulation of a router using OpenWrt and DPDK for latency
arbitration in high frequency trading (HFT).

```
  +------------+   eth0   +------------+   eth1   +-------------+
  |  Generator |--------- |  OpenWrt   |--------- |  Workstation |
  | (feed)     |  WAN     |  routertc  |  br-lan  | (strategy)   |
  +------------+ 192.168.64.2  |    |  192.168.128.100  +-------------+
                      |  +------+------+  |
                      |  |  tc / netem  |  |      measured link
                      |  |  DPDK relay  |  |      tick-to-trade latency
                      |  +-------------+  |
                      +--------------------+
```

The router runs as a UTM virtual machine on macOS. We strip the kernel buffer
delay, inject propagation latency, and measure the effect on market data.

## Status

```
  language  : C / POSIX sh
  test      : PASSED            (make test)
  build     : make relay        (Linux + DPDK only)
  target    : OpenWrt on UTM (192.168.128.100)
  license   : MIT
```

## Table of contents

1. [Project goals](#project-goals)
2. [Topology](#topology)
3. [Repository layout](#repository-layout)
4. [Quick start](#quick-start)
5. [The four experiments](#the-four-experiments)
6. [Build and test](#build-and-test)
7. [Deploy onto the router](#deploy-onto-the-router)
8. [References](#references)
9. [License](#license)

## Project goals

Four staged experiments that build a full latency-arbitration toolchain on
top of a virtualized OpenWrt router.

| Goal | Title                                  | Deliverable                          | doc                     |
|------|----------------------------------------|--------------------------------------|-------------------------|
| 1    | Flash a router with OpenWrt            | UTM VM, root SSH, tc access          | `docs/01_openwrt_flash.md` |
| 2    | Active Queue Management experiments    | `scripts/aqm_fifo.sh`, `sqm_experiment.sh` | `docs/02_aqm.md`     |
| 3    | Kernel bypass simulation with DPDK     | C relay `dpdk/rte_market_relay.c`    | `docs/03_dpdk.md`       |
| 4    | Inject latency and jitter via netem    | `scripts/netem_latency.sh`           | `docs/04_netem.md`       |

## Topology

From the task scenario, the router sits at:

```
lo       127.0.0.1/8          loopback
eth0     192.168.64.2/24      WAN (host NAT out to the Internet)
eth1     slave of br-lan      LAN physical port
br-lan   192.168.128.100/24   LAN bridge the workstation attaches to
```

The experiments target `eth0` (WAN / hub-to-hub leg) or `br-lan` (LAN leg),
whichever best matches the effect you want to observe.

## Repository layout

```
routertc/
|-- Makefile                  build + test + install targets
|-- README.md
|-- LICENSE                   MIT
|-- dpdk/
|   |-- mktdata.h             mock UDP market-data wire format + mutation
|   |-- rte_market_relay.c    DPDK RX->TX kernel-bypass relay
|   `-- test_mktdata.c        DPDK-free unit test (runs on macOS)
|-- scripts/
|   |-- aqm_fifo.sh           strict FIFO with a microscopic buffer
|   |-- sqm_experiment.sh     CAKE / FQ-CoDel baseline comparison
|   |-- netem_latency.sh      HK->SG 22.4ms + 0.1ms jitter injection
|   |-- netem_reset.sh        tear down any qdisc experiment
|   `-- bench_rtt.sh          RTT / jitter measurement helper
`-- docs/
    |-- 01_openwrt_flash.md
    |-- 02_aqm.md
    |-- 03_dpdk.md
    `-- 04_netem.md
```

## Quick start

On macOS (this repo) run the DPDK-free self-test to confirm the message
format logic is sane:

```sh
make test
```

On the router (or any OpenWrt box) after deploying this tree:

```sh
ssh root@192.168.128.100
cd /root/routertc

# Goal 2: strict FIFO with an 8-packet buffer
./scripts/aqm_fifo.sh br-lan 8

# Goal 4: HK to Singapore propagation
./scripts/netem_latency.sh eth0 22.4ms 0.1ms

# measure from a workstation
./scripts/bench_rtt.sh 192.168.128.100
```

## The four experiments

### Goal 1 - OpenWrt foundation

Stock firmware hides the low-level networking. OpenWrt gives root SSH access
to the kernel and the `tc` (Traffic Control) subsystem so we can completely
manipulate how packets are queued, scheduled, and prioritised at the hardware
level. Our router is a UTM VM. See `docs/01_openwrt_flash.md`.

### Goal 2 - Active Queue Management

If the router buffers a market-data burst, packets arrive stale and unprofitable.
We never want to queue - transmit or drop. `scripts/aqm_fifo.sh` installs a
strict `pfifo` with a microscopic limit (default 8 packets); SQM/CAKE in
`scripts/sqm_experiment.sh` is the counter-case that tries to be fair by
smoothing bursts into queueing delay. See `docs/02_aqm.md`.

```
burst            strict FIFO (limit=8)          CAKE / fq_codel
 +---------+     +-----+   drop, never stall    +-----+  smooth into backlog
 | 50 pkt  | --> | max |------------------------| buf |-------------->
 +---------+     +-----+   immediate transmit / drop    queue, then drain
```

### Goal 3 - Kernel bypass with DPDK

Standard networking costs microseconds copying from the NIC to the kernel to
your app. `dpdk/rte_market_relay.c` reads packets straight from the NIC rings,
strips a mock UDP market-data header, alters one byte, and pushes them out the
second port instantly - fully bypassing the kernel stack. Measure the tick-to-
trade difference against a stock iptables router. DPDK is Linux-only, so the
relay builds on a Linux box (see `docs/03_dpdk.md`); the message logic itself
is validated on macOS with `make test`.

```
 +--------------+  rte_eth_rx_burst  +--------------+  rte_eth_tx_burst  +-------------+
 | NIC RX ring  |------------------>|  rte_market  |------------------>| NIC TX ring |
 +--------------+                   |  _relay      |                   +-------------+
                                     |  strip + flip byte 1
                                     +--------------+
    (kernel network stack is bypassed entirely)
```

### Goal 4 - Latency and jitter injection

`scripts/netem_latency.sh` replicates the physical propagation delay between
financial hubs - Hong Kong to Singapore is ~22.4 ms one way plus 0.1 ms jitter:

```sh
tc qdisc add dev eth0 root netem delay 22.4ms 0.1ms
```

Run an open-source backtester like HftBacktest across the link and watch
execution slip or miss because the router is artificially skewing timestamps.
See `docs/04_netem.md`.

## Build and test

`make test` compiles and runs the DPDK-free market-data self-test (works on
macOS and Linux):

```
  ok : sizeof(struct mkt_header) == 16
  ok : raw[15] == 0x80
  ok : delta == 0x01
  == result: PASSED (0 failures) ==
```

`make relay` builds the DPDK relay. It requires DPDK on a Linux host - it uses
`pkg-config --cflags/--libs libdpdk` when available, otherwise point
`RTE_SDK`/`RTE_TARGET` at your DPDK tree and build manually (see
`docs/03_dpdk.md`).

Build status legend:

```
  ok    verified locally (make test on macOS)
  linux requires a Linux host with DPDK (make relay)
```

## Deploy onto the router

The `Makefile` `install` target copies everything into the UTM router and makes
the scripts executable:

```sh
make install RTC_ROUTER=root@192.168.128.100
```

On the router confirm the interfaces from the task:

```sh
root@OpenWrt:~# ip addr show
root@OpenWrt:~# tc qdisc show
```

## References

- openwrt.org - flashing and SQM / Smart Queue Management
- DPDK: dpdk.org / dpdk.git - Data Plane Development Kit
- hftbacktest.readthedocs.io - open-source HFT backtester
- netem and iproute2: `man tc-netem`, `man tc`
- iptp.net / grid5000.fr / bufferspeed.com - latency measurement context

## License

MIT. See [LICENSE](LICENSE).

