/* Real driver RX code, with hardware PIO and netdev publication mocks only. */
#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* REAL_DECLARATIONS */

typedef struct { uint32_t offset; uint16_t length; } expected_frame_t;
static expected_frame_t expected[65];
static unsigned queued, delivered, consumed, io_reads, reset_writes;
static bool corrupt;
static uint8_t snapshot[RTL_RX_RING_SIZE + RTL_RX_WRAP_SLACK];

static uint8_t pattern(uint32_t i, unsigned ordinal) {
    return (uint8_t)(i * 29u + i / 251u + ordinal * 37u);
}

static uint8_t inb(uint16_t port) {
    assert(port == rtl8139_device.io_base + RTL_COMMAND);
    ++io_reads;
    return consumed < queued ? 0 : RTL_CMD_RX_EMPTY;
}

static void outb(uint16_t port, uint8_t value) {
    assert(corrupt && port == rtl8139_device.io_base + RTL_COMMAND);
    assert(value == (reset_writes == 0 ? 0 : RTL_CMD_RX_ENABLE | RTL_CMD_TX_ENABLE));
    assert(reset_writes < 2);
    ++reset_writes;
}

static void outw(uint16_t port, uint16_t value) {
    assert(port == rtl8139_device.io_base + RTL_CAPR);
    if (corrupt) {
        assert(reset_writes == 2 && delivered == 0 && value == 0xfff0u);
    } else {
        assert(consumed < queued && delivered == consumed + 1);
        uint32_t next = (expected[consumed].offset + expected[consumed].length + 11u) & ~3u;
        next %= RTL_RX_RING_SIZE;
        assert(rtl8139_device.rx_offset == next && value == (uint16_t)(next - 16u));
    }
    ++consumed;
}

static void netdev_deliver_rx(const uint8_t *frame, uint16_t length) {
    assert(!corrupt && delivered < queued);
    expected_frame_t item = expected[delivered];
    assert(length == item.length);
    if (item.offset + 4u + length <= RTL_RX_RING_SIZE)
        assert(frame == rtl8139_rx_buffer + item.offset + 4u); /* fast path */
    for (uint32_t i = 0; i < length; ++i) {
        if (frame[i] != pattern(i, delivered)) {
            fprintf(stderr, "RTL_RX_MISMATCH offset=%u length=%u byte=%u\n",
                    item.offset, length, i);
            assert(frame[i] == pattern(i, delivered));
        }
    }
    ++delivered;
}

/* REAL_FUNCTION */

static void prepare(uint32_t offset) {
    memset(rtl8139_rx_buffer, 0xa7, sizeof(rtl8139_rx_buffer));
    memset(&rtl8139_device, 0, sizeof(rtl8139_device));
    rtl8139_device.initialized = true;
    rtl8139_device.io_base = 0x1000;
    rtl8139_device.rx_offset = offset;
    rtl8139_rx_pending = true;
    queued = delivered = consumed = io_reads = reset_writes = 0;
    corrupt = false;
}

static uint32_t enqueue(uint32_t offset, uint16_t length) {
    assert((offset & 3u) == 0 && queued < 65);
    uint16_t dma_length = (uint16_t)(length + 4u);
    rtl8139_rx_buffer[offset] = RTL_RX_ROK;
    rtl8139_rx_buffer[offset + 1] = 0;
    rtl8139_rx_buffer[offset + 2] = (uint8_t)dma_length;
    rtl8139_rx_buffer[offset + 3] = (uint8_t)(dma_length >> 8);
    for (uint32_t i = 0; i < length; ++i)
        rtl8139_rx_buffer[(offset + 4u + i) % RTL_RX_RING_SIZE] = pattern(i, queued);
    for (uint32_t i = 0; i < 4; ++i)
        rtl8139_rx_buffer[(offset + 4u + length + i) % RTL_RX_RING_SIZE] = 0xfc;
    expected[queued++] = (expected_frame_t){offset, length};
    return ((offset + length + 11u) & ~3u) % RTL_RX_RING_SIZE;
}

static void save_ring(void) { memcpy(snapshot, rtl8139_rx_buffer, sizeof(snapshot)); }
static void check_ring(void) {
    /* Every DMA byte, including unused slack/guard bytes, remains untouched. */
    assert(memcmp(snapshot, rtl8139_rx_buffer, sizeof(snapshot)) == 0);
}

int main(void) {
    prepare(0);
    rtl8139_device.initialized = false;
    rtl8139_drain_rx();
    assert(io_reads == 0 && consumed == 0);
    rtl8139_device.initialized = true;
    rtl8139_drain_rx();
    assert(delivered == 0 && !rtl8139_rx_pending && io_reads == 2);

    /* Normal, every aligned wrap offset, header exactly at ring end,
     * FCS-only wrap, maximum and short lengths; repeat with fresh poison. */
    const uint16_t lengths[] = {0, 14, 60, 61, 1514, RTL_MAX_FRAME_SIZE};
    for (unsigned n = 0; n < sizeof(lengths) / sizeof(lengths[0]); ++n) {
        for (uint32_t offset = 0; offset < RTL_RX_RING_SIZE; offset += 4u) {
            if (offset != 0 && offset < RTL_RX_RING_SIZE - RTL_MAX_FRAME_SIZE - 12u) continue;
            prepare(offset);
            enqueue(offset, lengths[n]);
            save_ring();
            rtl8139_drain_rx();
            assert(delivered == 1 && consumed == 1 && !rtl8139_rx_pending);
            assert(reset_writes == 0);
            check_ring();
        }
    }

    /* Reject before any publication; old bounded recovery sequence retained. */
    const uint16_t bad_lengths[] = {0, 1, 3, RTL_MAX_FRAME_SIZE + 5u, 65535};
    for (unsigned n = 0; n <= sizeof(bad_lengths) / sizeof(bad_lengths[0]); ++n) {
        prepare(RTL_RX_RING_SIZE - 4u);
        enqueue(RTL_RX_RING_SIZE - 4u, 60);
        corrupt = true;
        if (n == sizeof(bad_lengths) / sizeof(bad_lengths[0])) {
            rtl8139_rx_buffer[RTL_RX_RING_SIZE - 4u] = 0;
        } else {
            rtl8139_rx_buffer[RTL_RX_RING_SIZE - 2u] = (uint8_t)bad_lengths[n];
            rtl8139_rx_buffer[RTL_RX_RING_SIZE - 1u] = (uint8_t)(bad_lengths[n] >> 8);
        }
        save_ring();
        rtl8139_drain_rx();
        assert(delivered == 0 && consumed == 1 && reset_writes == 2);
        assert(rtl8139_device.rx_offset == 0 && !rtl8139_rx_pending);
        check_ring();
    }

    prepare(RTL_RX_RING_SIZE - 100u);
    uint32_t offset = rtl8139_device.rx_offset;
    for (unsigned i = 0; i < 65; ++i) offset = enqueue(offset, 60);
    save_ring();
    rtl8139_drain_rx();
    assert(delivered == 64 && consumed == 64 && rtl8139_rx_pending && io_reads == 66);
    rtl8139_drain_rx();
    assert(delivered == 65 && consumed == 65 && !rtl8139_rx_pending);
    check_ring();
    puts("RTL8139_RECEIVE_WRAP_OK");
    return 0;
}
