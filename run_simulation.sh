#!/bin/bash
# Generates training/validation data and the baseline Pareto sweep.
# Run after building: gcc traffic_simulator.c -o traffic_simulator -lm -O2
set -e
SECONDS_PER_RUN=4

echo ">> training data (Poisson + MMPP burst/quiet traffic)"
./traffic_simulator --seconds=$SECONDS_PER_RUN --dump-features --policy=always \
    --sample=0.01 --seed=1 --out=train_baseline.csv

echo ">> held-out data (self-similar Pareto bursts + diurnal load)"
./traffic_simulator --seconds=$SECONDS_PER_RUN --dump-features --policy=always \
    --heavy-tail --pareto-alpha=1.4 --diurnal-amp=0.5 \
    --sample=0.01 --seed=3 --out=train_heavytail_diurnal.csv

echo ">> baseline Pareto sweep (sleep fraction vs tail latency)"
./traffic_simulator --seconds=$SECONDS_PER_RUN --policy=always --seed=2 \
    --out=pareto_always.csv
for T in 5000 2000 1000 500 200 100; do
    ./traffic_simulator --seconds=$SECONDS_PER_RUN --policy=baseline \
        --threshold=$T --sleep-us=100 --seed=2 --out=pareto_threshold_$T.csv
done

echo ">> model operating point (requires model_decision.h from train_model.py)"
./traffic_simulator --seconds=$SECONDS_PER_RUN --policy=model --seed=2 \
    --out=pareto_model.csv || echo "   (skipped: rebuild after training to include the model)"

echo "done."
