# Adaptive Micro-Sleep Scheduling for DPDK Polling

A machine-learning controller that decides when a DPDK poll-mode driver should
briefly sleep to save CPU power during idle periods, instead of busy-polling at
100% utilization. This repository contains the simulator used to generate
training data and baselines, the model training code, and a real DPDK
application used to measure in-loop inference cost.

## The problem

A DPDK poll-mode driver reads packets directly from the NIC by continuously
polling a receive ring, bypassing the kernel. This achieves very high
throughput but keeps the polling CPU core at 100% utilization even when no
traffic arrives. DPDK's existing remedy (the `l3fwd-power` sample and the
`rte_power_pmd_mgmt` "pause" mode) sleeps after a fixed number of consecutive
empty polls. We ask whether a learned policy can make a better sleep decision
from simple features of recent traffic.

## Repository contents

| File | What it is |
|------|------------|
| `traffic_simulator.c` | Discrete-event simulator of a polling loop under synthetic traffic. Generates training/validation data and baseline results. |
| `dpdk_application.c` | Real DPDK poll-loop application. Runs the trained model inside an actual `rte_eth_rx_burst` loop and measures nanoseconds-per-inference. |
| `model_decision.h` | The trained decision tree exported to C (via `m2cgen`), included by both programs. |
| `train_model.py` | Loads the simulator data, trains the models, and exports the tree to C. |
| `run_simulation.sh` | Generates the training data and the baseline Pareto sweep. |
| `*.csv` | Generated data: training sets and baseline sweep results. |

## How the pieces fit together

1. **`traffic_simulator.c`** generates packet arrivals using standard traffic
   models. Poisson arrivals, a Markov-modulated burst/quiet process (MMPP),
   and an optional Pareto heavy-tail mode for self-similar traffic
   (Kleinrock 1975; Fischer & Meier-Hellstern 1993; Leland et al. 1994). It
   runs a polling loop over those arrivals and, at each empty poll, records
   five features plus an oracle label (the best sleep duration in hindsight).
   A simulator is necessary because the controller's features and its
   sleep-versus-latency tradeoff depend on an asynchronous packet producer,
   which DPDK's `net_pcap` virtual device does not provide.

2. **`train_model.py`** trains a decision tree and a logistic-regression model
   on that data and exports the tree to `model_decision.h`.

3. **`dpdk_application.c`** includes `model_decision.h` and runs the trained
   policy inside a real DPDK loop on a cloud VM, where it measures the cost of
   model inference in the packet fast path.

## Features and decision

At each empty poll the controller sees five features:

- `empty_run`: Consecutive empty polls so far
- `us_since_pkt`: us since the last packet
- `rate_fast` / `rate_med` / `rate_slow`: EWMA estimates of packets-per-poll
  at three timescales (alpha = 0.20 / 0.02 / 0.002)

The controller makes a **binary** decision: sleep, or keep polling.

## Building and running

Simulator (no DPDK needed):

```sh
gcc traffic_simulator.c -o traffic_simulator -lm -O2
./run_simulation.sh                      # generate data + baseline sweep
```

Training (Python, e.g. in Colab or locally):

```sh
pip install scikit-learn m2cgen pandas
python3 train_model.py                   # trains models, writes model_decision.h
```

DPDK application (on a Linux VM with DPDK installed):

```sh
gcc dpdk_application.c -o dpdk_application $(pkg-config --cflags --libs libdpdk)
sudo ./dpdk_application -l 0-1 \
     --vdev 'net_pcap0,rx_pcap=/path/to/bigFlows.pcap' \
     -- --policy=model --seconds=30 --out=results_model.csv
```

## Experiment summary and results


**Modeling:** First framed the controller as a four-way choice over sleep
durations {0, 20, 100, 250} microseconds. That model never beat the
majority-class baseline and gave zero recall to the two longest sleeps,
regardless of class weighting or added features. The confusion matrix showed
100 and 250 microseconds were systematically merged. The cause is structural:
how deep one already is into a quiet period is nearly memoryless (exponential
inter-arrival gaps), so sleep *depth* is not predictable from polling history.
We therefore reduced the action space to a binary sleep / poll decision. This
is also architecturally sound: a long idle period is handled as a sequence of
short, re-evaluated sleeps, making the policy adaptive within an idle period.

**Binary model accuracy:** The depth-6 decision tree reached 0.823 test
accuracy (majority-class baseline 0.799) and 0.820 on held-out traffic of a
different type (self-similar Pareto bursts + diurnal load) that it never
trained on. A logistic-regression model agreed closely (0.809). The tree relies
mainly on the medium- and long-horizon arrival-rate features (rate_med,
rate_slow), i.e. it learned to detect the sustained-traffic-versus-quiet
regime.

**Power/latency tradeoff (simulator):** Sweeping the baseline's empty-poll
threshold traces a Pareto frontier from no sleeping (0% sleep, 0 microsecond
added p99 latency) up to aggressive sleeping (~65% sleep, ~97 microsecond p99).
The learned policy occupies an operating point near the aggressive end
(~86% sleep fraction at comparable tail latency).

**Inference cost (real DPDK):** Running the exported tree inside an unmodified
DPDK poll loop on a cloud VM on GCP, mean inference cost was 16.9 ns per call, versus
11.7 ns for the threshold baseline's single integer comparison. Both are a
small fraction of a poll (low hundreds of ns), confirming a tree-based
controller is cheap enough to run inline.

## Scope and limitations

- The simulator is a behavioral model of polling dynamics, not a
  cycle-accurate NIC emulator.
- Training data is synthetic. Validating on real captures replayed at line
  rate through a two-machine (traffic-generator + device-under-test) testbed
  is the natural next step.
- The DPDK runs here use the `net_pcap` software device, which has no
  asynchronous producer. This setup is used to measure
  inference cost, not the power/latency tradeoff (that comes from the
  simulator).

## References

- L. Kleinrock, *Queueing Systems, Volume 1: Theory*, Wiley, 1975.
- W. Fischer and K. Meier-Hellstern, "The Markov-modulated Poisson process
  (MMPP) cookbook," *Performance Evaluation*, 1993.
- W. Leland, M. Taqqu, W. Willinger, D. Wilson, "On the Self-Similar Nature of
  Ethernet Traffic," *IEEE/ACM Transactions on Networking*, 1994.
- DPDK Programmer's Guide, "Power Management,"
  https://doc.dpdk.org/guides/prog_guide/power_man.html
