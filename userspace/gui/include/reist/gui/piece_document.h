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

#endif
