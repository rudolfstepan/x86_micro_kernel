/** @file piece_document.h
 * @brief Fixed-capacity piece table for windowed Ring-3 documents.
 */
#ifndef REIST_GUI_PIECE_DOCUMENT_H
#define REIST_GUI_PIECE_DOCUMENT_H

#include <stddef.h>
#include <stdint.h>

#define REIST_GUI_PIECE_DOCUMENT_API_VERSION 1U
#define REIST_GUI_PIECE_CAPACITY 256U
#define REIST_GUI_PIECE_ADDED_CAPACITY 65536U
#define REIST_GUI_PIECE_IO_CAPACITY 4096U
#define REIST_GUI_PIECE_WRAP_INDEX_CAPACITY 16384U
#define REIST_GUI_PIECE_WRAP_ROW_BYTE_CAPACITY 256U
#define REIST_GUI_PIECE_WRAP_HARD_WORDS \
    ((REIST_GUI_PIECE_WRAP_INDEX_CAPACITY + 31U) / 32U)

enum reist_gui_piece_source { REIST_GUI_PIECE_ORIGINAL = 1U,
                              REIST_GUI_PIECE_ADDED = 2U };
enum reist_gui_piece_status { REIST_GUI_PIECE_OK = 0,
                              REIST_GUI_PIECE_EINVAL = -22,
                              REIST_GUI_PIECE_EIO = -5,
                              REIST_GUI_PIECE_ECAPACITY = -28 };

typedef struct reist_gui_piece {
    uint32_t source;
    uint32_t offset;
    uint32_t length;
} reist_gui_piece_t;

typedef int (*reist_gui_piece_read_fn)(void *context, uint32_t offset,
                                       void *data, uint32_t size);
typedef int (*reist_gui_piece_write_fn)(void *context, const void *data,
                                        uint32_t size);

typedef struct reist_gui_piece_document {
    uint32_t api_version;
    uint32_t struct_size;
    uint32_t size;
    uint32_t original_size;
    uint32_t piece_count;
    uint32_t added_used;
    uint32_t modified;
    reist_gui_piece_read_fn read_original;
    void *read_context;
    reist_gui_piece_t pieces[REIST_GUI_PIECE_CAPACITY];
    uint8_t added[REIST_GUI_PIECE_ADDED_CAPACITY];
} reist_gui_piece_document_t;

/**
 * Incrementally built visual-row index for one fixed wrap width.
 *
 * Each published row offset is an RFC 3629 scalar boundary.  A hard-start bit
 * marks rows beginning after a source line break; other rows are virtual and
 * add no byte to the document.  Partial indices are never consumers' truth:
 * callers may use row offsets only after complete becomes one.
 */
typedef struct reist_gui_piece_wrap_index {
    uint32_t api_version;
    uint32_t struct_size;
    uint32_t columns;
    uint32_t document_size;
    uint32_t row_count;
    uint32_t scanned_offset;
    uint32_t current_column;
    uint32_t current_row_bytes;
    uint32_t complete;
    uint32_t row_offsets[REIST_GUI_PIECE_WRAP_INDEX_CAPACITY];
    uint32_t hard_starts[REIST_GUI_PIECE_WRAP_HARD_WORDS];
} reist_gui_piece_wrap_index_t;

void reist_gui_piece_document_initialize(reist_gui_piece_document_t *document);
int reist_gui_piece_document_open(reist_gui_piece_document_t *document,
                                  uint32_t original_size,
                                  reist_gui_piece_read_fn read_original,
                                  void *read_context);
int reist_gui_piece_document_read(const reist_gui_piece_document_t *document,
                                  uint32_t offset, void *data, uint32_t size);
int reist_gui_piece_document_insert(reist_gui_piece_document_t *document,
                                    uint32_t offset, const void *data,
                                    uint32_t size);
int reist_gui_piece_document_erase(reist_gui_piece_document_t *document,
                                   uint32_t offset, uint32_t size);
int reist_gui_piece_document_stream(const reist_gui_piece_document_t *document,
                                    reist_gui_piece_write_fn write_bytes,
                                    void *write_context);

/** Start an unpublished index for document_size bytes and a nonzero width. */
void reist_gui_piece_wrap_index_initialize(
    reist_gui_piece_wrap_index_t *index, uint32_t columns,
    uint32_t document_size);
/**
 * Scan at most byte_budget source bytes.  Returns one when complete, zero when
 * more work remains, or a piece-document error.  byte_budget must be >= 4.
 */
int reist_gui_piece_wrap_index_advance(
    const reist_gui_piece_document_t *document,
    reist_gui_piece_wrap_index_t *index, uint32_t byte_budget);
/** Invalidate only the indexed logical-line suffix containing byte_offset. */
int reist_gui_piece_wrap_index_invalidate(
    reist_gui_piece_wrap_index_t *index, uint32_t byte_offset,
    uint32_t document_size);
/** Query whether row starts after a physical CR, LF or CRLF separator. */
uint32_t reist_gui_piece_wrap_index_row_hard(
    const reist_gui_piece_wrap_index_t *index, uint32_t row);
/** Map a byte offset to its containing row, or UINT32_MAX if unpublished. */
uint32_t reist_gui_piece_wrap_index_row_for_offset(
    const reist_gui_piece_wrap_index_t *index, uint32_t byte_offset);

#endif
