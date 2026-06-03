/*
 * dpdk_application.c : Instrumented DPDK poll loop for the adaptive
 *                      micro-sleep project.
 *
 *   This program runs a DPDK Poll Mode Driver (PMD) receive loop and, on each
 *   iteration, decides whether to insert a brief "micro-sleep" to save CPU
 *   power during idle periods. Three policies can be selected at runtime:
 *
 *     --policy=always     never sleep (the latency floor / power ceiling)
 *     --policy=baseline   sleep after a fixed number of consecutive empty
 *                         polls -- this mirrors the threshold-based scheme in
 *                         DPDK's own l3fwd-power sample application and its
 *                         rte_power_pmd_mgmt "pause" mode
 *     --policy=model      use the trained decision tree in model_decision.h
 *
 *
 * NOTE: net_pcap virtual device
 *   When driven by DPDK's net_pcap virtual device, packets are delivered only
 *   when polled and the capture file drains once, so this program is used here
 *   to validate the pipeline and measure inference cost.
 *   This is not used to produce busy/idle tradeoff (which requires an asynchronous
 *   producer; see the simulator).
 *
 * BUILD
 *   gcc dpdk_application.c -o dpdk_application $(pkg-config --cflags --libs libdpdk)
 *
 *   sudo ./dpdk_application -l 0-1 \
 *        --vdev 'net_pcap0,rx_pcap=/home/USER/bigFlows.pcap' \
 *        -- --policy=model --seconds=30 --out=results_model.csv
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <getopt.h>
#include <inttypes.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_cycles.h>

#include "model_decision.h"   /* the trained decision tree (exported to C) */

/* ----- fixed configuration ----- */
#define RECEIVE_RING_SIZE        1024
#define MBUF_POOL_SIZE           1000000  /* sized for bigFlows.pcap (791,615 pkts) */
#define MBUF_CACHE_SIZE          256
#define RECEIVE_BURST_SIZE       32
#define FEATURE_COUNT            5

/*
 * Exponentially-weighted moving-average (EWMA) smoothing factors for the three
 * arrival-rate features, spaced by roughly a factor of ten so they capture
 * short-, medium-, and long-horizon traffic rate. Effective memory of an EWMA
 * with factor alpha is about 1/alpha samples (so ~5, ~50, ~500 polls here).
 * These MUST match the values used in the simulator, or a model trained on
 * simulator data would see different inputs at deployment.
 */
static const double EWMA_ALPHA_FAST = 0.20;
static const double EWMA_ALPHA_MED  = 0.02;
static const double EWMA_ALPHA_SLOW = 0.002;

/* When the model (or baseline) decides to sleep, how long to sleep, in us. */
#define SLEEP_DURATION_US 100.0

/* Runtime-selectable policy */
enum sleep_policy {
    POLICY_ALWAYS,    /* never sleep */
    POLICY_BASELINE,  /* fixed empty-poll threshold */
    POLICY_MODEL      /* trained decision tree */
};

static enum sleep_policy selected_policy   = POLICY_BASELINE;
static uint64_t          empty_poll_threshold = 1000; /* for the baseline policy */
static uint64_t          run_duration_seconds = 30;
static char              output_path[256]      = "results.csv";

static volatile int stop_requested = 0;
static void handle_interrupt(int signal_number) { (void)signal_number; stop_requested = 1; }

/*
 * decide_should_sleep: The policy dispatch. Returns 1 to sleep, 0 to keep
 * polling. The feature_vector is filled by the caller in the order documented
 * in model_decision.h.
 */
static inline int decide_should_sleep(uint64_t consecutive_empty_polls,
                                       const double *feature_vector)
{
    switch (selected_policy) {
        case POLICY_ALWAYS:
            return 0;
        case POLICY_BASELINE:
            return consecutive_empty_polls > empty_poll_threshold;
        case POLICY_MODEL:
            return model_predicts_sleep(feature_vector);
    }
    return 0;
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

/* Parse the application-specific arguments that follow the EAL "--" separator. */
static void parse_application_arguments(int argc, char **argv)
{
    static struct option long_options[] = {
        {"policy",    required_argument, 0, 'p'},
        {"threshold", required_argument, 0, 't'},
        {"seconds",   required_argument, 0, 'd'},
        {"out",       required_argument, 0, 'o'},
        {0, 0, 0, 0}
    };
    int option, option_index;
    while ((option = getopt_long(argc, argv, "p:t:d:o:",
                                 long_options, &option_index)) != -1) {
        switch (option) {
            case 'p':
                if      (!strcmp(optarg, "always"))   selected_policy = POLICY_ALWAYS;
                else if (!strcmp(optarg, "baseline")) selected_policy = POLICY_BASELINE;
                else if (!strcmp(optarg, "model"))    selected_policy = POLICY_MODEL;
                else { fprintf(stderr, "unknown policy: %s\n", optarg); exit(1); }
                break;
            case 't': empty_poll_threshold = strtoull(optarg, NULL, 10); break;
            case 'd': run_duration_seconds = strtoull(optarg, NULL, 10); break;
            case 'o': strncpy(output_path, optarg, sizeof(output_path) - 1); break;
            default: break;
        }
    }
}

/* Configure and start one receive/transmit queue on the given port. */
static int initialize_port(uint16_t port_id, struct rte_mempool *mbuf_pool)
{
    struct rte_eth_conf port_configuration;
    memset(&port_configuration, 0, sizeof(port_configuration));

    if (rte_eth_dev_configure(port_id, 1, 1, &port_configuration) != 0)
        return -1;

    uint16_t num_rx_descriptors = RECEIVE_RING_SIZE;
    uint16_t num_tx_descriptors = RECEIVE_RING_SIZE;
    if (rte_eth_dev_adjust_nb_rx_tx_desc(port_id,
            &num_rx_descriptors, &num_tx_descriptors) != 0)
        return -1;

    if (rte_eth_rx_queue_setup(port_id, 0, num_rx_descriptors,
            rte_eth_dev_socket_id(port_id), NULL, mbuf_pool) != 0)
        return -1;
    if (rte_eth_tx_queue_setup(port_id, 0, num_tx_descriptors,
            rte_eth_dev_socket_id(port_id), NULL) != 0)
        return -1;

    return rte_eth_dev_start(port_id);
}

int main(int argc, char **argv)
{
    /* DPDK's Environment Abstraction Layer consumes its own leading args. */
    int eal_args_consumed = rte_eal_init(argc, argv);
    if (eal_args_consumed < 0)
        rte_exit(EXIT_FAILURE, "EAL initialization failed\n");
    argc -= eal_args_consumed;
    argv += eal_args_consumed;

    parse_application_arguments(argc, argv);
    signal(SIGINT, handle_interrupt);

    if (rte_eth_dev_count_avail() == 0)
        rte_exit(EXIT_FAILURE, "no ethernet ports found (did you pass --vdev?)\n");
    const uint16_t port_id = 0;

    struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create(
        "MBUF_POOL", MBUF_POOL_SIZE, MBUF_CACHE_SIZE, 0,
        RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "failed to create mbuf pool\n");

    if (initialize_port(port_id, mbuf_pool) != 0)
        rte_exit(EXIT_FAILURE, "failed to initialize port %u\n", port_id);

    FILE *output_file = fopen(output_path, "w");
    if (output_file == NULL)
        rte_exit(EXIT_FAILURE, "cannot open output file %s\n", output_path);
    fprintf(output_file,
            "time_s,policy,empty_polls,full_polls,packets,sleeps,"
            "skip_fraction,nanoseconds_per_inference,packets_missed\n");

    /* Timing setup. rte_rdtsc() reads the CPU timestamp counter; tsc_hz is its
     * frequency, used to convert cycle counts to seconds/nanoseconds. */
    const uint64_t tsc_hz             = rte_get_tsc_hz();
    const uint64_t run_end_tsc        = rte_rdtsc() + run_duration_seconds * tsc_hz;
    const uint64_t window_length_tsc  = tsc_hz / 10;  /* 100 ms reporting window */
    uint64_t       window_end_tsc     = rte_rdtsc() + window_length_tsc;

    /* Per-window aggregate counters (reset each window). */
    uint64_t empty_poll_count        = 0;
    uint64_t full_poll_count         = 0;
    uint64_t packet_count            = 0;
    uint64_t sleep_count             = 0;
    uint64_t inference_cycles_total  = 0;
    uint64_t inference_call_count    = 0;

    /* Running feature state. */
    double   rate_fast               = 0.0;
    double   rate_med                = 0.0;
    double   rate_slow               = 0.0;
    uint64_t consecutive_empty_polls = 0;
    uint64_t last_packet_tsc         = rte_rdtsc();

    int window_index = 0;
    struct rte_mbuf *received_packets[RECEIVE_BURST_SIZE];

    while (!stop_requested && rte_rdtsc() < run_end_tsc) {
        uint16_t num_received = rte_eth_rx_burst(
            port_id, 0, received_packets, RECEIVE_BURST_SIZE);

        /* --- update features (SAME ORDER as the simulator) --- */
        rate_fast = EWMA_ALPHA_FAST * num_received + (1.0 - EWMA_ALPHA_FAST) * rate_fast;
        rate_med  = EWMA_ALPHA_MED  * num_received + (1.0 - EWMA_ALPHA_MED ) * rate_med;
        rate_slow = EWMA_ALPHA_SLOW * num_received + (1.0 - EWMA_ALPHA_SLOW) * rate_slow;

        if (num_received == 0) {
            empty_poll_count++;
            consecutive_empty_polls++;
        } else {
            full_poll_count++;
            packet_count += num_received;
            consecutive_empty_polls = 0;
            last_packet_tsc = rte_rdtsc();
            for (uint16_t i = 0; i < num_received; i++)
                rte_pktmbuf_free(received_packets[i]);
        }

        double feature_vector[FEATURE_COUNT];
        feature_vector[0] = (double)consecutive_empty_polls;
        feature_vector[1] = (double)(rte_rdtsc() - last_packet_tsc) * 1e6 / (double)tsc_hz;
        feature_vector[2] = rate_fast;
        feature_vector[3] = rate_med;
        feature_vector[4] = rate_slow;

        /* Time the decision -- this is the headline inference-cost measurement. */
        uint64_t inference_start_tsc = rte_rdtsc();
        int should_sleep = decide_should_sleep(consecutive_empty_polls, feature_vector);
        inference_cycles_total += rte_rdtsc() - inference_start_tsc;
        inference_call_count++;

        if (should_sleep) {
            sleep_count++;
            rte_delay_us_block((unsigned)SLEEP_DURATION_US);
        }

        /* Flush one window of aggregate statistics. */
        uint64_t now_tsc = rte_rdtsc();
        if (now_tsc >= window_end_tsc) {
            struct rte_eth_stats port_stats;
            rte_eth_stats_get(port_id, &port_stats);

            uint64_t total_polls = empty_poll_count + full_poll_count;
            double skip_fraction = total_polls
                ? (double)sleep_count / (double)total_polls : 0.0;
            double nanoseconds_per_inference = inference_call_count
                ? (double)inference_cycles_total * 1e9
                  / (double)tsc_hz / (double)inference_call_count : 0.0;

            fprintf(output_file,
                "%.1f,%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                ",%.4f,%.2f,%" PRIu64 "\n",
                (window_index + 1) * 0.1, policy_name(),
                empty_poll_count, full_poll_count, packet_count, sleep_count,
                skip_fraction, nanoseconds_per_inference, port_stats.imissed);

            empty_poll_count = full_poll_count = packet_count = 0;
            sleep_count = inference_cycles_total = inference_call_count = 0;
            window_end_tsc = now_tsc + window_length_tsc;
            window_index++;
        }
    }

    fclose(output_file);
    printf("done -> %s\n", output_path);
    rte_eth_dev_stop(port_id);
    rte_eal_cleanup();
    return 0;
}
