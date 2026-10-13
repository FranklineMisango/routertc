# Goal 3 - Kernel Bypass Simulation with DPDK

Standard OS networking costs microseconds: NIC -> kernel -> your process.
HFT developers bypass the kernel and read packets straight from the NIC rings.

## Hardware

An old desktop PC with a multi-port Intel network card acting as the router.
Two of the ports form the forwarding pair (RX -> TX); the DPDK app owns them
directly and the kernel never sees those frames.

## Install DPDK

On that Linux box (Ubuntu/Debian example):

```sh
sudo apt-get install -y build-essential linux-headers-$(uname -r) \
    pkg-config libnuma-dev
git clone https://dpdk.org/git/dpdk-stable /opt/dpdk-stable-22.11
cd /opt/dpdk-stable-22.11
meson build
cd build
meson configure -Dexamples=all
ninja
sudo ninja install
sudo ldconfig
```

## Enable huge pages and bind the NIC

```sh
sudo modprobe vfio-pci
sudo dpdk-devbind.py --status
sudo dpdk-devbind.py --bind=vfio-pci <pci-addr-of-port1> <pci-addr-of-port2>
```

Add huge pages:

```sh
echo 'vm.nr_hugepages=2048' | sudo tee -a /etc/sysctl.conf
sudo sysctl -p
```

## Build the relay

DPDK is Linux-only, so building must happen on the Linux box, not macOS. From
this repository:

```sh
export RTE_SDK=/opt/dpdk-stable-22.11
export RTE_TARGET=x86_64-native-linuxapp-gcc
make relay
```

or, if `libdpdk.pc` is installed system-wide, `make relay` will use
`pkg-config` automatically.

## Run the relay

```sh
./rte_market_relay -l 0 -a 0000:04:00.0 -a 0000:04:00.1 \
    --file-prefix=rtc -- --rx-port 0 --tx-port 1
```

What it does per frame:

1. Reads a burst straight from the RX NIC ring (`rte_eth_rx_burst`).
2. Locates the mock market-data header after the Ethernet header.
3. Strips it, then alters a single byte (`flags ^= 0x01`).
4. Pushes the frame out the TX port instantly (`rte_eth_tx_burst`).

## Source layout

| File                       | Purpose                                        |
|----------------------------|------------------------------------------------|
| `dpdk/rte_market_relay.c`  | The DPDK RX->TX relay worker                   |
| `dpdk/mktdata.h`           | Mock UDP market-data wire format + mutation    |
| `dpdk/test_mktdata.c`      | DPDK-free unit test (runs on macOS too)        |

## Measuring tick-to-trade latency

Compare DPDK vs a stock iptables router on the same two ports:

- Baseline: keep the kernel stack and `iptables` forwarding in place, run a
  traffic generator, measure with `ping`/`pingpong` or a PTP timestamp.
- DPDK: run the relay above, drive the same generator, and read the relay's
  frame counter and the NIC hardware timestamps.

The delta is the kernel-bypass win.

## Test the message logic anywhere

`test_mktdata.c` does not need DPDK, so you can validate the header and byte
mutation on macOS:

```sh
make test
```
