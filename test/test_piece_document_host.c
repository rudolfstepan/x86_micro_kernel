/** @file test_piece_document_host.c */
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "reist/gui/piece_document.h"

static const uint8_t original[] = "alpha-beta-gamma";
static uint8_t output[128];
static uint32_t output_used, read_calls;
static int read_original(void *context, uint32_t offset, void *data, uint32_t size) {
    (void)context; ++read_calls;
    if (offset > sizeof(original) - 1U || size > sizeof(original) - 1U - offset) return -1;
    memcpy(data, original + offset, size); return 0;
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
    return 0;
}
