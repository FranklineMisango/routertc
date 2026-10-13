/*
 * mktdata.h
 *
 * routertc - mock "UDP Market Data" wire format used by the DPDK relay.
 *
 * This is intentionally NOT the real UDP/IP header. It models an application
 * payload that your trading feed would carry after the L2/L3/L4 headers have
 * already been parsed. The DPDK relay strips this header, mutates a single
 * byte, and re-emits the frame on the outbound port - the same "normalisation"
 * step an HFT normaliser performs at nanosecond scale.
 *
 * Layout is fixed, little-endian, tightly packed so no padding leaks into wire
 * transfers.
 */

#ifndef MKT_H_INCLUDED
#define MKT_H_INCLUDED

#include <stdint.h>

/* Fixed-size mock market data header (16 bytes). */
#pragma pack(push, 1)
struct mkt_header {
    uint32_t seq;        /* monotonically increasing sequence number   */
    uint64_t ts_ns;      /* nanosecond wall-clock timestamp            */
    uint16_t symbol_id;  /* instrument id                              */
    uint8_t  msg_type;   /* 0x01 = trade, 0x02 = quote                 */
    uint8_t  flags;      /* bit0 = odd lot, bit1 = auction             */
};

/* Variable trade/quote body carried after the header. */
struct mkt_body {
    uint32_t price_raw;  /* fixed point price, Q26 format              */
    uint64_t volume;     /* shares / lots                              */
};
#pragma pack(pop)

/* Byte offsets used by the relay so a single-byte mutation is unambiguous. */
enum {
    MKT_HDR_LEN = sizeof(struct mkt_header),
    MKT_FLAGS_OFFSET = 14,   /* relative to start of struct mkt_header */
    MKT_SYMBOL_OFFSET = 12,  /* symbol_id low byte                     */
};

/* Deterministic single-byte transformation used by the demo relay.
 * Flipping one bit of `flags` simulates the "alter a single byte" step. */
static inline uint8_t mkt_mutate_flags(uint8_t flags)
{
    return flags ^ 0x01u; /* toggle the odd-lot bit */
}

#endif /* MKT_H_INCLUDED */
