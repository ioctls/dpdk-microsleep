/*
 * traffic_simulator.c : Discrete-event simulator of a DPDK-style polling loop
 *                       under synthetic network traffic.
 *
 *   The features and objective of a micro-sleep controller depend on the
 *   asynchronous race between an independent packet *producer* (the network)
 *   and the polling *consumer* (the loop). DPDK's net_pcap virtual device has
 *   no independent producer. It delivers packets only when polled and drains
 *   a capture file exactly once, so it cannot generate realistic busy/idle
 *   interleaving or a genuine sleep-versus-latency tradeoff. This simulator
 *   supplies that missing producer. Packets are placed on a clock and become
 *   available only when simulated time reaches them, while the consumer
 *   advances the same clock as it works. Results are therefore deterministic
 *   (seeded) and independent of how fast the host CPU runs.
 *
 *   This is a BEHAVIORAL model of polling dynamics, not a cycle-accurate NIC
 *   emulator. There is no DMA, cache, or multi-queue modeling.
 *
 * ARRIVAL MODEL:
 *   1. Poisson arrivals at an average rate : The canonical model of
 *      independent arrivals.            [Kleinrock, Queueing Systems Vol.1, 1975]
 *   2. A two-state Markov chain modulates that rate between a high "burst"
 *      state and a low "quiet" state : A Markov-Modulated Poisson Process
 *      (MMPP), the standard way to model bursty traffic.
 *                          [Fischer & Meier-Hellstern, "The MMPP cookbook", 1993]
 *   3. Optionally (--heavy-tail), burst/quiet dwell times are drawn from a
 *      Pareto distribution instead of exponential, producing self-similar
 *      (long-range-dependent) traffic, the hallmark of real network traffic.
 *                          [Leland, Taqqu, Willinger & Wilson, IEEE/ACM ToN, 1994]
 *   A slow diurnal sinusoid (--diurnal-amp) can further modulate the rate to
 *   emulate time-of-day load.
 *
 * OUTPUT MODES:
 *   --dump-features : one row per empty-poll decision: the feature vector plus
 *                     the oracle's best sleep duration. This is the ML
 *                     training/validation data.
 *   (default)       : one row per 100 ms window: policy, sleep fraction, and
 *                     p50/p99/p99.9 latency. This is the baseline results data,
 *                     swept across thresholds to trace a Pareto frontier.
 *
 * POLICIES:
 *   --policy=always   : never sleep
 *   --policy=baseline : sleep after a fixed empty-poll threshold (DPDK-style)
 *   --policy=model    : use the trained decision tree in model_decision.h
 *
 * ORACLE LABEL (exact, computed from the known future arrival schedule)
 *   At an empty poll, the oracle picks the LARGEST sleep duration from a fixed
 *   menu whose worst-case added latency stays within an SLO budget i.e. a sleep of
 *   S microseconds is acceptable if the next arrival is at least S - SLO away.
 *   Because the full arrival schedule is generated up front, this lookahead is
 *   exact rather than estimated.
 *
 * BUILD
 *   gcc traffic_simulator.c -o traffic_simulator -lm -O2
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <getopt.h>

#include "model_decision.h"   /* the trained decision tree (exported to C) */

/* =====================================================================
 * Seedable pseudo-random number generator (xorshift128+ with a splitmix64
 * seeding step). Chosen for speed and reproducibility; the specific generator
 * does not affect the statistical model, only the stream of random draws.
 * ===================================================================== */
static uint64_t rng_state_0 = 0x9e3779b97f4a7c15ULL;
static uint64_t rng_state_1 = 0xbf58476d1ce4e5b9ULL;

static void seed_random_generator(uint64_t seed)
{
    uint64_t mixed = seed + 0x9e3779b97f4a7c15ULL;
    mixed = (mixed ^ (mixed >> 30)) * 0xbf58476d1ce4e5b9ULL;
    mixed = (mixed ^ (mixed >> 27)) * 0x94d049bb133111ebULL;
    rng_state_0 = mixed ^ (mixed >> 31);

    mixed = seed + 0x7f4a7c159e3779b9ULL;
    mixed = (mixed ^ (mixed >> 30)) * 0xbf58476d1ce4e5b9ULL;
    mixed = (mixed ^ (mixed >> 27)) * 0x94d049bb133111ebULL;
    rng_state_1 = mixed ^ (mixed >> 31);

    if (rng_state_0 == 0 && rng_state_1 == 0) rng_state_0 = 1;
}

static inline uint64_t next_random_u64(void)
{
    uint64_t x = rng_state_0, y = rng_state_1;
    rng_state_0 = y;
    x ^= x << 23;
    rng_state_1 = x ^ y ^ (x >> 17) ^ (y >> 26);
    return rng_state_1 + y;
}

/* Uniform double in [0, 1). */
static inline double random_uniform(void)
{
    return (next_random_u64() >> 11) * (1.0 / 9007199254740992.0);
}

/* Number of Poisson events with the given mean (Knuth's small-mean method). */
static inline int random_poisson(double mean)
{
    if (mean <= 0.0) return 0;
    double probability = exp(-mean);
    double cumulative   = probability;
    double draw         = random_uniform();
    int    count        = 0;
    while (draw > cumulative && count < 1000) {
        count++;
        probability *= mean / count;
        cumulative  += probability;
    }
    return count;
}

/* Exponentially distributed value with the given mean. */
static inline double random_exponential(double mean)
{
    return -mean * log(1.0 - random_uniform());
}

/* Pareto-distributed value (shape alpha, scale minimum). Heavy-tailed. */
static inline double random_pareto(double shape_alpha, double scale_minimum)
{
    return scale_minimum / pow(1.0 - random_uniform(), 1.0 / shape_alpha);
}

/* =====================================================================
 * The fixed sleep-duration menu (the oracle's action set), in microseconds.
 * MUST match the menu the model was trained against.
 * ===================================================================== */
#define SLEEP_MENU_SIZE 4
static const double SLEEP_MENU_US[SLEEP_MENU_SIZE] = {0.0, 20.0, 100.0, 250.0};

/* EWMA smoothing factors -- MUST match dpdk_application.c. */
static const double EWMA_ALPHA_FAST = 0.20;
static const double EWMA_ALPHA_MED  = 0.02;
static const double EWMA_ALPHA_SLOW = 0.002;

/* =====================================================================
 * Tunable parameters (all overridable on the command line).
 * ===================================================================== */
static double   arrival_base_rate_per_us = 0.01;  /* baseline packets / us       */
static double   burst_rate_multiplier    = 10.0;  /* rate factor in burst state  */
static double   burst_mean_dwell_us      = 200.0; /* mean burst duration         */
static double   quiet_mean_dwell_us      = 800.0; /* mean quiet duration         */
static int      use_heavy_tail_dwell     = 0;     /* Pareto dwell if set         */
static double   pareto_shape_alpha       = 1.5;   /* 1<alpha<2 => self-similar   */
static double   diurnal_amplitude        = 0.0;   /* 0 disables diurnal cycle    */
static double   diurnal_period_us        = 1.0e7; /* diurnal period (10 s)       */
static double   poll_duration_us         = 0.2;   /* simulated time per poll     */
static double   arrival_generation_step_us = 0.1; /* arrival sampling resolution */
static double   simulated_seconds        = 4.0;   /* simulation horizon          */
static double   slo_budget_us            = 10.0;  /* latency budget for oracle   */
static double   baseline_sleep_us        = 100.0; /* sleep length for baseline   */
static uint64_t baseline_empty_threshold = 1000;  /* baseline empty-poll trigger */
static int      receive_queue_capacity   = 4096;  /* max queued packets          */
static uint64_t random_seed              = 42;
static double   feature_dump_sample_rate = 0.02;  /* fraction of empty polls kept */
static int      dump_features            = 0;
static char     output_path[256]         = "simulation.csv";

#define RECEIVE_BURST_SIZE 32
#define LATENCY_HISTOGRAM_SIZE 8192   /* 1 us buckets; final bucket is overflow */

enum sleep_policy { POLICY_ALWAYS, POLICY_BASELINE, POLICY_MODEL };
static enum sleep_policy selected_policy = POLICY_BASELINE;

/*
 * decide_sleep_duration_us -- returns the sleep length the chosen policy wants,
 * in microseconds (0 means keep polling). The model returns a binary decision,
 * which we map to {0, baseline_sleep_us}; this matches the project's finding
 * that sleep *depth* is not learnable, so the controller decides only whether
 * to sleep and re-evaluates after each fixed-length sleep.
 */
static inline double decide_sleep_duration_us(uint64_t consecutive_empty_polls,
                                              const double *feature_vector)
{
    switch (selected_policy) {
        case POLICY_ALWAYS:
            return 0.0;
        case POLICY_BASELINE:
            return (consecutive_empty_polls > baseline_empty_threshold)
                   ? baseline_sleep_us : 0.0;
        case POLICY_MODEL:
            return model_predicts_sleep(feature_vector) ? baseline_sleep_us : 0.0;
    }
    return 0.0;
}

static const char *policy_name(void)
{
    switch (selected_policy) {
        case POLICY_ALWAYS:   return "always";
        case POLICY_BASELINE: return "baseline";
        case POLICY_MODEL:    return "model";
    }
    return "unknown";
}

static void parse_arguments(int argc, char **argv)
{
    static struct option long_options[] = {
        {"base-rate",         required_argument, 0, 1},
        {"burst-factor",      required_argument, 0, 2},
        {"burst-mean-us",     required_argument, 0, 3},
        {"quiet-mean-us",     required_argument, 0, 4},
        {"heavy-tail",        no_argument,       0, 5},
        {"pareto-alpha",      required_argument, 0, 6},
        {"diurnal-amp",       required_argument, 0, 7},
        {"diurnal-period-us", required_argument, 0, 8},
        {"poll-us",           required_argument, 0, 9},
        {"seconds",           required_argument, 0, 10},
        {"slo-us",            required_argument, 0, 11},
        {"threshold",         required_argument, 0, 12},
        {"queue-cap",         required_argument, 0, 13},
        {"seed",              required_argument, 0, 14},
        {"sample",            required_argument, 0, 15},
        {"policy",            required_argument, 0, 16},
        {"out",               required_argument, 0, 17},
        {"dump-features",     no_argument,       0, 18},
        {"sleep-us",          required_argument, 0, 19},
        {0, 0, 0, 0}
    };
    int option, option_index;
    while ((option = getopt_long(argc, argv, "", long_options, &option_index)) != -1) {
        switch (option) {
            case 1:  arrival_base_rate_per_us  = atof(optarg); break;
            case 2:  burst_rate_multiplier     = atof(optarg); break;
            case 3:  burst_mean_dwell_us        = atof(optarg); break;
            case 4:  quiet_mean_dwell_us        = atof(optarg); break;
            case 5:  use_heavy_tail_dwell       = 1;            break;
            case 6:  pareto_shape_alpha         = atof(optarg); break;
            case 7:  diurnal_amplitude          = atof(optarg); break;
            case 8:  diurnal_period_us          = atof(optarg); break;
            case 9:  poll_duration_us           = atof(optarg); break;
            case 10: simulated_seconds          = atof(optarg); break;
            case 11: slo_budget_us              = atof(optarg); break;
            case 12: baseline_empty_threshold   = strtoull(optarg, NULL, 10); break;
            case 13: receive_queue_capacity     = atoi(optarg); break;
            case 14: random_seed                = strtoull(optarg, NULL, 10); break;
            case 15: feature_dump_sample_rate   = atof(optarg); break;
            case 16:
                if      (!strcmp(optarg, "always"))   selected_policy = POLICY_ALWAYS;
                else if (!strcmp(optarg, "baseline")) selected_policy = POLICY_BASELINE;
                else if (!strcmp(optarg, "model"))    selected_policy = POLICY_MODEL;
                else { fprintf(stderr, "unknown policy: %s\n", optarg); exit(1); }
                break;
            case 17: strncpy(output_path, optarg, sizeof(output_path) - 1); break;
            case 18: dump_features = 1; break;
            case 19: baseline_sleep_us = atof(optarg); break;
        }
    }
}

/* Return the q-th percentile (q in [0,1]) from a 1 us-bucket histogram. */
static double percentile_from_histogram(const uint64_t *histogram,
                                        uint64_t total_samples, double q)
{
    if (total_samples == 0) return 0.0;
    uint64_t target = (uint64_t)(q * total_samples);
    uint64_t cumulative = 0;
    for (int bucket = 0; bucket < LATENCY_HISTOGRAM_SIZE; bucket++) {
        cumulative += histogram[bucket];
        if (cumulative >= target) return (double)bucket;
    }
    return (double)(LATENCY_HISTOGRAM_SIZE - 1);
}

int main(int argc, char **argv)
{
    parse_arguments(argc, argv);
    seed_random_generator(random_seed);

    /* ----------------------------------------------------------------
     * Phase 1: generate the complete arrival schedule (sorted timestamps).
     * The Markov chain flips between burst and quiet states; within each
     * step we draw Poisson arrivals at the current modulated rate.
     * ---------------------------------------------------------------- */
    double horizon_us = simulated_seconds * 1.0e6;
    size_t schedule_capacity = (1 << 20);
    size_t arrival_count = 0;
    double *arrival_times_us = malloc(schedule_capacity * sizeof(double));

    int in_burst_state = 0;
    double state_change_time = use_heavy_tail_dwell
        ? random_pareto(pareto_shape_alpha, quiet_mean_dwell_us)
        : random_exponential(quiet_mean_dwell_us);

    for (double time_us = 0.0; time_us < horizon_us;
         time_us += arrival_generation_step_us) {

        if (time_us >= state_change_time) {
            in_burst_state = !in_burst_state;
            double mean_dwell = in_burst_state ? burst_mean_dwell_us : quiet_mean_dwell_us;
            double dwell = use_heavy_tail_dwell
                ? random_pareto(pareto_shape_alpha, mean_dwell)
                : random_exponential(mean_dwell);
            state_change_time = time_us + dwell;
        }

        double diurnal_factor =
            1.0 + diurnal_amplitude * sin(2.0 * M_PI * time_us / diurnal_period_us);
        if (diurnal_factor < 0.01) diurnal_factor = 0.01;

        double current_rate = arrival_base_rate_per_us * diurnal_factor
                            * (in_burst_state ? burst_rate_multiplier : 1.0);

        int arrivals_this_step = random_poisson(current_rate * arrival_generation_step_us);
        for (int i = 0; i < arrivals_this_step; i++) {
            if (arrival_count == schedule_capacity) {
                schedule_capacity *= 2;
                arrival_times_us = realloc(arrival_times_us,
                                           schedule_capacity * sizeof(double));
            }
            arrival_times_us[arrival_count++] = time_us;
        }
    }
    fprintf(stderr, "[simulator] generated %zu arrivals over %.2f s (mean %.4f pkt/us)\n",
            arrival_count, simulated_seconds, arrival_count / horizon_us);

    /* ----------------------------------------------------------------
     * Phase 2: run the polling loop over the schedule in simulated time.
     * ---------------------------------------------------------------- */
    FILE *output_file = fopen(output_path, "w");
    if (output_file == NULL) { perror("fopen"); return 1; }

    /* Receive queue (ring buffer of arrival timestamps still waiting). */
    double *queue = malloc(sizeof(double) * receive_queue_capacity);
    int queue_head = 0, queue_tail = 0, queue_size = 0;

    size_t next_unadmitted_arrival = 0;
    double simulated_now_us  = 0.0;
    double last_packet_time_us = 0.0;

    double rate_fast = 0.0, rate_med = 0.0, rate_slow = 0.0;
    uint64_t consecutive_empty_polls = 0;
    uint64_t total_packets = 0, total_drops = 0;
    double   total_sleep_us = 0.0;

    uint64_t latency_all[LATENCY_HISTOGRAM_SIZE];     memset(latency_all, 0, sizeof(latency_all));
    uint64_t latency_window[LATENCY_HISTOGRAM_SIZE];  memset(latency_window, 0, sizeof(latency_window));

    double   window_length_us = 100000.0;  /* 100 ms */
    double   window_end_us     = window_length_us;
    double   window_start_us    = 0.0;
    uint64_t window_packets     = 0;
    double   window_sleep_us    = 0.0;
    int      window_index       = 0;

    uint64_t oracle_label_histogram[SLEEP_MENU_SIZE];
    memset(oracle_label_histogram, 0, sizeof(oracle_label_histogram));

    if (dump_features)
        fprintf(output_file,
                "empty_run,us_since_pkt,rate_fast,rate_med,rate_slow,best_sleep\n");
    else
        fprintf(output_file,
                "time_s,policy,sleep_fraction,p50_us,p99_us,p999_us,drops,packets\n");

    while (simulated_now_us < horizon_us) {
        /* Admit every packet whose arrival time has now passed. */
        while (next_unadmitted_arrival < arrival_count &&
               arrival_times_us[next_unadmitted_arrival] <= simulated_now_us) {
            if (queue_size < receive_queue_capacity) {
                queue[queue_tail] = arrival_times_us[next_unadmitted_arrival];
                queue_tail = (queue_tail + 1) % receive_queue_capacity;
                queue_size++;
            } else {
                total_drops++;   /* queue full: packet dropped */
            }
            next_unadmitted_arrival++;
        }

        /* Drain up to a burst's worth of packets, recording their wait time. */
        int num_received = 0;
        while (num_received < RECEIVE_BURST_SIZE && queue_size > 0) {
            double arrival_time = queue[queue_head];
            queue_head = (queue_head + 1) % receive_queue_capacity;
            queue_size--;
            double wait_us = simulated_now_us - arrival_time;
            int bucket = (int)wait_us;
            if (bucket < 0) bucket = 0;
            if (bucket >= LATENCY_HISTOGRAM_SIZE) bucket = LATENCY_HISTOGRAM_SIZE - 1;
            latency_all[bucket]++;
            latency_window[bucket]++;
            num_received++;
        }

        /* Update features in the SAME ORDER as dpdk_application.c. */
        rate_fast = EWMA_ALPHA_FAST * num_received + (1.0 - EWMA_ALPHA_FAST) * rate_fast;
        rate_med  = EWMA_ALPHA_MED  * num_received + (1.0 - EWMA_ALPHA_MED ) * rate_med;
        rate_slow = EWMA_ALPHA_SLOW * num_received + (1.0 - EWMA_ALPHA_SLOW) * rate_slow;

        if (num_received == 0) {
            consecutive_empty_polls++;
        } else {
            consecutive_empty_polls = 0;
            last_packet_time_us = simulated_now_us;
            total_packets += num_received;
            window_packets += num_received;
        }

        double feature_vector[5];
        feature_vector[0] = (double)consecutive_empty_polls;
        feature_vector[1] = simulated_now_us - last_packet_time_us;
        feature_vector[2] = rate_fast;
        feature_vector[3] = rate_med;
        feature_vector[4] = rate_slow;

        /* On a sampled subset of empty polls, emit the feature row plus the
         * exact oracle label computed from the known future schedule. */
        if (dump_features && num_received == 0 &&
            random_uniform() < feature_dump_sample_rate) {
            double budget = (next_unadmitted_arrival < arrival_count)
                ? (arrival_times_us[next_unadmitted_arrival] - simulated_now_us) + slo_budget_us
                : 1e18;
            double best_sleep = 0.0;
            int    best_index = 0;
            for (int menu = SLEEP_MENU_SIZE - 1; menu >= 0; menu--) {
                if (SLEEP_MENU_US[menu] <= budget) {
                    best_sleep = SLEEP_MENU_US[menu];
                    best_index = menu;
                    break;
                }
            }
            oracle_label_histogram[best_index]++;
            fprintf(output_file, "%.0f,%.2f,%.5f,%.5f,%.5f,%.0f\n",
                    feature_vector[0], feature_vector[1],
                    feature_vector[2], feature_vector[3], feature_vector[4],
                    best_sleep);
        }

        /* Apply the policy's sleep decision and advance the clock. */
        double sleep_us = decide_sleep_duration_us(consecutive_empty_polls, feature_vector);
        if (sleep_us > 0.0) {
            total_sleep_us  += sleep_us;
            window_sleep_us += sleep_us;
            simulated_now_us += sleep_us;
        } else {
            simulated_now_us += poll_duration_us;
        }

        /* Flush one window of aggregate statistics (results mode only). */
        if (!dump_features && simulated_now_us >= window_end_us) {
            uint64_t window_total = 0;
            for (int b = 0; b < LATENCY_HISTOGRAM_SIZE; b++) window_total += latency_window[b];
            double elapsed = simulated_now_us - window_start_us;
            double sleep_fraction = elapsed > 0 ? window_sleep_us / elapsed : 0.0;

            fprintf(output_file, "%.1f,%s,%.4f,%.0f,%.0f,%.0f,%llu,%llu\n",
                    (window_index + 1) * window_length_us / 1e6, policy_name(),
                    sleep_fraction,
                    percentile_from_histogram(latency_window, window_total, 0.50),
                    percentile_from_histogram(latency_window, window_total, 0.99),
                    percentile_from_histogram(latency_window, window_total, 0.999),
                    (unsigned long long)total_drops,
                    (unsigned long long)window_packets);

            memset(latency_window, 0, sizeof(latency_window));
            window_end_us  += window_length_us;
            window_start_us = simulated_now_us;
            window_packets  = 0;
            window_sleep_us = 0.0;
            window_index++;
        }
    }

    /* Final global summary to stderr. */
    uint64_t grand_total = 0;
    for (int b = 0; b < LATENCY_HISTOGRAM_SIZE; b++) grand_total += latency_all[b];
    double global_sleep_fraction = horizon_us > 0 ? total_sleep_us / horizon_us : 0.0;

    fprintf(stderr, "[simulator] policy=%s sleep_fraction=%.3f packets=%llu drops=%llu "
            "p50=%.0f p99=%.0f p999=%.0f us -> %s\n",
            policy_name(), global_sleep_fraction,
            (unsigned long long)grand_total, (unsigned long long)total_drops,
            percentile_from_histogram(latency_all, grand_total, 0.50),
            percentile_from_histogram(latency_all, grand_total, 0.99),
            percentile_from_histogram(latency_all, grand_total, 0.999),
            output_path);

    if (dump_features) {
        fprintf(stderr, "[simulator] oracle label distribution (sleep us -> count):");
        for (int m = 0; m < SLEEP_MENU_SIZE; m++)
            fprintf(stderr, " %.0f=%llu",
                    SLEEP_MENU_US[m], (unsigned long long)oracle_label_histogram[m]);
        fprintf(stderr, "\n");
    }

    fclose(output_file);
    free(arrival_times_us);
    free(queue);
    return 0;
}
