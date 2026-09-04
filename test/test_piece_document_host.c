/** @file test_piece_document_host.c */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "reist/gui/piece_document.h"

static const uint8_t original[] = "alpha-beta-gamma";
static uint8_t output[128];
static uint32_t output_used, read_calls;
static int read_original(void *context, uint32_t offset, void *data, uint32_t size) {
    const uint8_t *source = context != 0 ? context : original;
    ++read_calls;
    memcpy(data, source + offset, size); return 0;
}
static int write_output(void *context, const void *data, uint32_t size) {
    (void)context;
    if (size > sizeof(output) - output_used) return -1;
    memcpy(output + output_used, data, size); output_used += size; return 0;
}
int main(void) {
    reist_gui_piece_document_t document;
    assert(reist_gui_piece_document_open(&document, sizeof(original) - 1U,
                                         read_original, 0) == 0);
    uint8_t window[8];
    assert(reist_gui_piece_document_read(&document, 6U, window, 4U) == 0);
    assert(memcmp(window, "beta", 4U) == 0 && read_calls == 1U);
    assert(reist_gui_piece_document_insert(&document, 6U, "BIG-", 4U) == 0);
    assert(reist_gui_piece_document_erase(&document, 10U, 5U) == 0);
    assert(reist_gui_piece_document_insert(&document, document.size, "!", 1U) == 0);
    output_used = 0U;
    assert(reist_gui_piece_document_stream(&document, write_output, 0) == 0);
    assert(output_used == 16U && memcmp(output, "alpha-BIG-gamma!", 16U) == 0);
    reist_gui_piece_document_t snapshot = document;
    uint8_t too_large[REIST_GUI_PIECE_ADDED_CAPACITY];
    assert(reist_gui_piece_document_insert(&document, 0U, too_large,
               sizeof(too_large)) == REIST_GUI_PIECE_ECAPACITY);
    assert(document.size == snapshot.size && document.piece_count == snapshot.piece_count &&
           document.added_used == snapshot.added_used &&
           memcmp(document.pieces, snapshot.pieces, sizeof(document.pieces)) == 0);
    reist_gui_piece_document_initialize(&document);
    assert(reist_gui_piece_document_insert(&document, 0U, "new", 3U) == 0);
    output_used = 0U;
    assert(reist_gui_piece_document_stream(&document, write_output, 0) == 0);
    assert(output_used == 3U && memcmp(output, "new", 3U) == 0);

    static const uint8_t wrapped_original[] =
        "0123456789abcdefghij\n"
        "\xC3\xA4\xC3\xB6\xC3\xBC-ende";
    assert(reist_gui_piece_document_open(
               &document, sizeof(wrapped_original) - 1U,
               read_original, 0) == 0);
    document.read_context = (void *)wrapped_original;
    reist_gui_piece_wrap_index_t index;
    reist_gui_piece_wrap_index_initialize(
        &index, 10U, document.size);
    while (!index.complete)
        assert(reist_gui_piece_wrap_index_advance(
                   &document, &index, 7U) >= 0);
    assert(index.row_count == 3U);
    assert(index.row_offsets[0] == 0U);
    assert(index.row_offsets[1] == 10U);
    assert(index.row_offsets[2] == 21U);
    assert(reist_gui_piece_wrap_index_row_hard(&index, 2U) == 1U);
    assert(reist_gui_piece_wrap_index_row_for_offset(&index, 15U) == 1U);

    assert(reist_gui_piece_document_insert(&document, 5U, "XX", 2U) == 0);
    assert(reist_gui_piece_wrap_index_invalidate(
               &index, 5U, document.size) == 0);
    assert(index.complete == 0U && index.row_count == 1U);
    while (!index.complete)
        assert(reist_gui_piece_wrap_index_advance(
                   &document, &index, 11U) >= 0);
    assert(index.row_count == 4U);
    assert(index.row_offsets[index.row_count - 1U] == 23U);

    static uint8_t capacity_original[REIST_GUI_PIECE_WRAP_INDEX_CAPACITY + 1U];
    memset(capacity_original, 'x', sizeof(capacity_original));
    assert(reist_gui_piece_document_open(
               &document, sizeof(capacity_original), read_original, 0) == 0);
    document.read_context = capacity_original;
    reist_gui_piece_wrap_index_initialize(&index, 1U, document.size);
    int capacity_status = 0;
    while (!index.complete && capacity_status >= 0)
        capacity_status = reist_gui_piece_wrap_index_advance(
            &document, &index, REIST_GUI_PIECE_IO_CAPACITY);
    assert(capacity_status == REIST_GUI_PIECE_ECAPACITY);
    return 0;
}
