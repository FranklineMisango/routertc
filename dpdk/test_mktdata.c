/*
 * test_mktdata.c
 *
 * Minimal, DPDK-free unit test for the mock market-data header described in
 * mktdata.h and the single-byte mutation used by rte_market_relay.c.
 *
 * This file exists so the message-format logic can be validated on a plain
 * host (macOS / the VM dev box) without a full DPDK build. Compile with:
 *
 *   cc -std=c11 -Wall -Wextra -I. dpdk/test_mktdata.c -o test_mktdata && ./test_mktdata
 *
 * It checks:
 *   - the header is tightly packed to exactly the expected wire size
 *   - the field offsets line up with the documented byte indices
 *   - the flag mutation toggles exactly one bit
 *   - the sequence-number increment round-trips correctly
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>

#include "mktdata.h"

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (cond) {                                                     \
            printf("ok   : %s\n", #cond);                               \
        } else {                                                        \
            printf("FAIL : %s  [%s:%d]\n", #cond, __FILE__, __LINE__);  \
            failures++;                                                 \
        }                                                               \
    } while (0)

int main(void)
{
    printf("== routertc mktdata header self-test ==\n\n");

    /* 16-byte fixed header, no padding introduced by the compiler. */
    CHECK(sizeof(struct mkt_header) == 16);
    printf("      sizeof(struct mkt_header) = %zu\n", sizeof(struct mkt_header));

    /* Field offsets relative to the struct base. */
    CHECK(offsetof(struct mkt_header, seq)        == 0);
    CHECK(offsetof(struct mkt_header, ts_ns)      == 4);
    CHECK(offsetof(struct mkt_header, symbol_id)  == 12);
    CHECK(offsetof(struct mkt_header, msg_type)   == 14);
    CHECK(offsetof(struct mkt_header, flags)      == 15);

    /* Packed body is also fixed size. */
    CHECK(sizeof(struct mkt_body) == 12);

    /* Documented offsets match the enum constants. */
    CHECK(MKT_FLAGS_OFFSET == 14);
    CHECK(MKT_SYMBOL_OFFSET == 12);

    /* Single-byte mutation toggles exactly one bit. */
    {
        uint8_t a = 0x82;
        uint8_t b = mkt_mutate_flags(a);
        uint8_t delta = a ^ b;
        CHECK(delta == 0x01);
    }

    /* Round-trip: build a header, serialise, confirm the flags byte is byte
     * index 15 total and mutating it changes exactly the expected position. */
    {
        struct mkt_header h;
        unsigned char raw[16];

        memset(&h, 0, sizeof(h));
        h.seq       = 0x01020304;
        h.ts_ns     = 0x1122334455667788ULL;
        h.symbol_id = 0xABCD;
        h.msg_type  = 0x01;
        h.flags     = 0x80; /* auction bit set */

        memcpy(raw, &h, sizeof(h));

        /* flags byte is the 16th byte of the packed struct. */
        CHECK(raw[15] == 0x80);
        CHECK(raw[14] == 0x01);   /* msg_type */
        CHECK(raw[13] == 0xAB && raw[12] == 0xCD); /* symbol_id, LE */

        /* Apply mutation and confirm exactly the flagged bit toggled. */
        h.flags = mkt_mutate_flags(h.flags);
        memcpy(raw, &h, sizeof(h));
        CHECK(raw[15] == (0x80 ^ 0x01));
    }

    printf("\n== result: %s (%d failures) ==\n",
           failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
