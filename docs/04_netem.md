# Goal 4 - Inject Latency and Jitter via netem

Strategies must account for the physical speed of light. You can replicate
geographical distance and jitter across your living room using the kernel's
native network emulator, `netem`.

## The reference scenario

Hong Kong to Singapore is roughly:

```
one-way propagation  : ~22.4 ms
random jitter        : ~0.1 ms
```

## Apply the delay

`scripts/netem_latency.sh` wraps the exact command from the task:

```sh
./scripts/netem_latency.sh eth0 22.4ms 0.1ms
```

Which expands to the canonical:

```sh
tc qdisc add dev eth0 root netem delay 22.4ms 0.1ms
```

## Other distances worth modelling

| Route                | One-way delay | Script invocation                        |
|----------------------|---------------|------------------------------------------|
| Hong Kong - Singapore | ~22.4 ms      | `netem_latency.sh eth0 22.4ms 0.1ms`    |
| Dense metro hub      | ~0.3 ms       | `netem_latency.sh eth0 0.3ms 0.02ms`     |
| Cross-continent leg  | slow + loss   | `netem_latency.sh eth0 60ms 2ms`         |

## Running an HFT backtest across the link

Install an open-source backtester such as `HftBacktest` (see hftbacktest on
PyPI/readthedocs) on a host behind the router, point its data feed at the
remote generator, and replay the market-data feed:

```
your generator  -->  [eth0 22.4ms netem]  -->  router  -->  HftBacktest
```

Watch for:

- Execution slip: orders arrive after the timestamp you thought you had.
- Timestamp skew: netem inflates apparent arrival times so PTP/PTPv2 deltas
  become meaningless on that leg.
- Missed fills: the injected jitter causes your strategy to skip microsecond
  windows it would otherwise have captured.

## Measurement

From a LAN workstation:

```sh
./scripts/bench_rtt.sh 192.168.128.100 100 0.3
```

Compare min / avg / std-dev before and after applying netem.

## Cleanup

```sh
./scripts/netem_reset.sh eth0
```
