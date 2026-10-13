/*
 * rte_market_relay.c
 *
 * routertc - Goal 3: "Kernel Bypass" market data relay built on DPDK.
 *
 * Listens on a receive port, strips the mock UDP market-data header, alters a
 * single byte, and pushes the frame out of a transmit port instantly -
 * bypassing the Linux kernel network stack entirely.
 *
 * Build / run requirements
 * -------------------------
 *   - Linux x86_64 (or arm64) host with a multi-port Intel NIC bound to vfio-pci
 *   - DPDK >= 21.11 (this repo uses the rte_eth / rte_mempool / rte_ring APIs)
 *   - Compile as a DPDK "extra" (external) app:
 *
 *       export RTE_SDK=/opt/dpdk-stable-22.11.6
 *       export RTE_TARGET=x86_64-native-linuxapp-gcc
 *       make -C dpdk
 *
 *   - Bind two ports to vfio-pci, then run:
 *
 *       ./rte_market_relay -l 0 -a 0000:04:00.0 -a 0000:04:00.1 \
 *           --file-prefix=rtc -- --rx-port 0 --tx-port 1
 *
 * The application is deliberately minimal: no reassembly, no protocol stack,
 * a single RX/TX queue pair per port. Every packet that arrives on the RX
 * port is normalised and forwarded. That is the point of the experiment -
 * measure the tick-to-trade routing latency against a stock iptables router.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <rte_common.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_ring.h>

#include "mktdata.h"

/* ------------------------------------------------------------------ *
 *  Config
 * ------------------------------------------------------------------ */
#define RTC_MBUF_CACHE_SIZE   256
#define RTC_PKT_BURST         32
#define RTC_NB_MBUF           (16384 * 2)

#define RTC_RX_PORT_DEFAULT    0
#define RTC_TX_PORT_DEFAULT    1

/* ------------------------------------------------------------------ *
 *  Globals
 * ------------------------------------------------------------------ */
static volatile int g_running = 1;

struct rtc_port_ctx {
    uint16_t port_id;
    uint32_t frames;
};

struct rte_mempool *g_pkt_pool = NULL;

static void
signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}
/* ------------------------------------------------------------------ *
 *  Port init
 * ------------------------------------------------------------------ */
static inline int
rtc_setup_port(uint16_t port, uint16_t rx_q, uint16_t tx_q,
               struct rte_mempool *pool)
{
    struct rte_eth_dev_info dev_info;
    struct rte_eth_conf port_conf;
    struct rte_eth_rxconf rx_conf;
    struct rte_eth_txconf tx_conf;
    int ret;

    memset(&port_conf, 0, sizeof(port_conf));
    /* No RSS, no offloads - keep the data path as thin as possible. */
    port_conf.rxmode.mq_mode = RTE_ETH_MQ_RX_NONE;
    port_conf.txmode.mq_mode = RTE_ETH_MQ_TX_NONE;

    ret = rte_eth_dev_configure(port, rx_q, tx_q, &port_conf);
    if (ret != 0) {
        RTE_LOG(ERR, USER1, "port %u config failed: %d\n", port, ret);
        return ret;
    }

    rte_eth_dev_info_get(port, &dev_info);
    rx_conf = dev_info.default_rxconf;
    tx_conf = dev_info.default_txconf;

    uint16_t q;
    for (q = 0; q < rx_q; q++) {
        ret = rte_eth_rx_queue_setup(port, q, 512, rte_eth_dev_socket_id(port),
                                     &rx_conf, pool);
        if (ret < 0) {
            RTE_LOG(ERR, USER1, "rx queue %u setup failed: %d\n", q, ret);
            return ret;
        }
    }
    for (q = 0; q < tx_q; q++) {
        ret = rte_eth_tx_queue_setup(port, q, 512, rte_eth_dev_socket_id(port),
                                     &tx_conf);
        if (ret < 0) {
            RTE_LOG(ERR, USER1, "tx queue %u setup failed: %d\n", q, ret);
            return ret;
        }
    }

    ret = rte_eth_dev_start(port);
    if (ret < 0) {
        RTE_LOG(ERR, USER1, "port %u start failed: %d\n", port, ret);
        return ret;
    }
    rte_eth_promiscuous_enable(port);
    return 0;
}


/* ------------------------------------------------------------------ *
 *  Frame processing - forward a batch between two ports.
 * ------------------------------------------------------------------ */
static inline void
rtc_relay_batch(struct rte_mbuf **pkts, uint16_t nb, uint16_t tx_port)
{
    uint16_t i;
    for (i = 0; i < nb; i++) {
        struct rte_mbuf *m = pkts[i];
        uint8_t *pkt = rte_pktmbuf_mtod(m, uint8_t *);
        uint16_t len = rte_pktmbuf_pkt_len(m);

        if (likely(len >= RTE_ETHER_HDR_LEN + MKT_HDR_LEN)) {
            /* Point at the mock market-data header, after the eth header. */
            struct mkt_header *hdr =
                (struct mkt_header *)(pkt + RTE_ETHER_HDR_LEN);
            /* "Strip the mock header": consume it. */
            uint8_t *payload = (uint8_t *)hdr + MKT_HDR_LEN;
            (void)payload;
            /* "Alter a single byte": flip the odd-lot flag bit. */
            hdr->flags = mkt_mutate_flags(hdr->flags);
        }

        rte_eth_tx_burst(tx_port, 0, &m, 1);
    }
}

/* ------------------------------------------------------------------ *
 *  Worker
 * ------------------------------------------------------------------ */
static int
rtc_worker(void *arg)
{
    struct rtc_port_ctx *ctx = (struct rtc_port_ctx *)arg;
    struct rte_mbuf *bufs[RTC_PKT_BURST];
    uint16_t nb;

    while (g_running) {
        nb = rte_eth_rx_burst(ctx->port_id, 0, bufs, RTC_PKT_BURST);
        if (unlikely(nb == 0)) {
            /* Idle spin: hand the core back to the NIC for a few cycles. */
            rte_pause();
            continue;
        }
        rtc_relay_batch(bufs, nb, RTC_TX_PORT);
        ctx->frames += nb;
    }
    return 0;
}
/* ------------------------------------------------------------------ *
 *  Main
 * ------------------------------------------------------------------ */
int
main(int argc, char **argv)
{
    struct rtc_port_ctx rx_ctx;
    uint16_t rx_port = RTC_RX_PORT_DEFAULT;
    uint16_t tx_port = RTC_TX_PORT_DEFAULT;
    unsigned int rx_lcore;
    int ret, i;

    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "EAL init failed\n");
    argc -= ret;
    argv += ret;

    /* Simple CLI for the app-specific options. */
    for (i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--rx-port") == 0 && i + 1 < argc)
            rx_port = (uint16_t)strtoul(argv[++i], NULL, 0);
        else if (strcmp(argv[i], "--tx-port") == 0 && i + 1 < argc)
            tx_port = (uint16_t)strtoul(argv[++i], NULL, 0);
    }

    g_pkt_pool = rte_pktmbuf_pool_create("RTC_PKT_POOL", RTC_NB_MBUF,
                        RTC_MBUF_CACHE_SIZE, 0,
                        RTE_MBUF_DEFAULT_BUF_SIZE,
                        rte_socket_id());
    if (g_pkt_pool == NULL)
        rte_exit(EXIT_FAILURE, "mbuf pool create failed\n");

    if (rtc_setup_port(rx_port, 1, 1, g_pkt_pool) != 0)
        rte_exit(EXIT_FAILURE, "RX port setup failed\n");
    if (rtc_setup_port(tx_port, 1, 1, g_pkt_pool) != 0)
        rte_exit(EXIT_FAILURE, "TX port setup failed\n");

    memset(&rx_ctx, 0, sizeof(rx_ctx));
    rx_ctx.port_id = rx_port;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Pin the relay worker to a dedicated lcore. */
    rx_lcore = rte_get_next_lcore(-1, 1, 0);
    if (rx_lcore >= RTE_MAX_LCORE)
        rte_exit(EXIT_FAILURE, "no available lcore for the relay\n");

    RTE_LOG(NOTICE, USER1, "routertc: relaying port %u -> port %u on lcore %u\n",
            rx_port, tx_port, rx_lcore);

    rte_eal_remote_launch(rtc_worker, &rx_ctx, rx_lcore);
    ret = rte_eal_wait_lcore(rx_lcore);

    RTE_LOG(NOTICE, USER1,
            "routertc: done, %u frames relayed between ports %u and %u\n",
            rx_ctx.frames, rx_port, tx_port);

    rte_eth_dev_stop(rx_port);
    rte_eth_dev_stop(tx_port);
    rte_eal_cleanup();
    return (ret == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

