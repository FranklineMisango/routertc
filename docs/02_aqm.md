# Goal 2 - Active Queue Management (AQM) Experiments

HFT problem: if the router buffers a market-data burst, those packets arrive
late and your strategy has already missed the move. You want packets transmitted
immediately or dropped - never queued.

## The two sides of the experiment

### 1. HFT style strict FIFO with a microscopic buffer

`scripts/aqm_fifo.sh` replaces the root qdisc with a `pfifo` of limit 8 packets
(default). No CAKE or FQ-CoDel "fairness" - just a drain queue too small to
smooth a burst.

```
./scripts/aqm_fifo.sh br-lan 8
```

### 2. SQM / CAKE baseline for comparison

`scripts/sqm_experiment.sh` installs CAKE with its bandwidth auto-probing and
per-flow fairness. Run it, measure, then flip back to the strict FIFO.

```
./scripts/sqm_experiment.sh br-lan
```

## What to observe

| Metric                    | Strict FIFO (limit=8)     | CAKE / FQ-CoDel           |
|---------------------------|---------------------------|---------------------------|
| Burst larger than buffer  | instant drops             | smoothed into queueing    |
| Added latency per packet  | near zero until full      | rises with queue depth    |
| Predictability            | high (drop, no stall)     | low (stall, then drain)   |

Capture the stats after loading a burst traffic generator:

```
tc -s qdisc show dev br-lan
```

Watch the `dropped` and `backlog` counters. Under the FIFO you will see drops
spike the instant the burst exceeds the buffer. Under CAKE you will see
`backlog` grow and latency climb - the exact behaviour an HFT router must avoid.

## Cleanup

```
./scripts/netem_reset.sh br-lan
```
