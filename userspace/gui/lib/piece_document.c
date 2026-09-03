/** @file piece_document.c
 * @brief Transactional fixed-capacity piece-table implementation.
 */
#include "reist/gui/piece_document.h"

static void bytes_copy(void *destination, const void *source, uint32_t size) {
    uint8_t *to = (uint8_t *)destination;
    const uint8_t *from = (const uint8_t *)source;
    for (uint32_t index = 0U; index < size; ++index) to[index] = from[index];
}

static void bytes_zero(void *destination, uint32_t size) {
    uint8_t *bytes = (uint8_t *)destination;
    for (uint32_t index = 0U; index < size; ++index) bytes[index] = 0U;
}

static int valid(const reist_gui_piece_document_t *document) {
    if (document == 0 ||
        document->api_version != REIST_GUI_PIECE_DOCUMENT_API_VERSION ||
        document->struct_size != sizeof(*document) ||
        document->piece_count > REIST_GUI_PIECE_CAPACITY ||
        document->added_used > REIST_GUI_PIECE_ADDED_CAPACITY) return 0;
    uint32_t total = 0U;
    for (uint32_t i = 0U; i < document->piece_count; ++i) {
        const reist_gui_piece_t *piece = &document->pieces[i];
        uint32_t bound = piece->source == REIST_GUI_PIECE_ORIGINAL
            ? document->original_size : document->added_used;
        if ((piece->source != REIST_GUI_PIECE_ORIGINAL &&
             piece->source != REIST_GUI_PIECE_ADDED) || piece->length == 0U ||
            piece->offset > bound || piece->length > bound - piece->offset ||
            piece->length > UINT32_MAX - total) return 0;
        total += piece->length;
    }
    return total == document->size;
}

void reist_gui_piece_document_initialize(reist_gui_piece_document_t *document) {
    if (document == 0) return;
    bytes_zero(document, sizeof(*document));
    document->api_version = REIST_GUI_PIECE_DOCUMENT_API_VERSION;
    document->struct_size = sizeof(*document);
}

int reist_gui_piece_document_open(reist_gui_piece_document_t *document,
                                  uint32_t original_size,
                                  reist_gui_piece_read_fn read_original,
                                  void *read_context) {
    if (document == 0 || (original_size != 0U && read_original == 0))
        return REIST_GUI_PIECE_EINVAL;
    reist_gui_piece_document_initialize(document);
    document->size = document->original_size = original_size;
    document->read_original = read_original;
    document->read_context = read_context;
    if (original_size != 0U) {
        document->pieces[0] = (reist_gui_piece_t){
            REIST_GUI_PIECE_ORIGINAL, 0U, original_size};
        document->piece_count = 1U;
    }
    return REIST_GUI_PIECE_OK;
}

int reist_gui_piece_document_read(const reist_gui_piece_document_t *document,
                                  uint32_t offset, void *data, uint32_t size) {
    if (!valid(document) || (size != 0U && data == 0) || offset > document->size ||
        size > document->size - offset) return REIST_GUI_PIECE_EINVAL;
    uint8_t *output = (uint8_t *)data;
    uint32_t logical = 0U, completed = 0U;
    for (uint32_t i = 0U; i < document->piece_count && completed < size; ++i) {
        const reist_gui_piece_t *piece = &document->pieces[i];
        if (offset >= logical + piece->length) { logical += piece->length; continue; }
        uint32_t within = offset > logical ? offset - logical : 0U;
        uint32_t amount = piece->length - within;
        if (amount > size - completed) amount = size - completed;
        if (piece->source == REIST_GUI_PIECE_ADDED)
            bytes_copy(output + completed,
                       document->added + piece->offset + within, amount);
        else if (document->read_original(document->read_context,
                                         piece->offset + within,
                                         output + completed, amount) != 0)
            return REIST_GUI_PIECE_EIO;
        completed += amount; offset += amount; logical += piece->length;
    }
    return completed == size ? REIST_GUI_PIECE_OK : REIST_GUI_PIECE_EIO;
}

static int append(reist_gui_piece_t *pieces, uint32_t *count,
                  uint32_t source, uint32_t offset, uint32_t length) {
    if (length == 0U) return 0;
    if (*count != 0U) {
        reist_gui_piece_t *last = &pieces[*count - 1U];
        if (last->source == source && last->offset + last->length == offset) {
            if (length > UINT32_MAX - last->length) return -1;
            last->length += length; return 0;
        }
    }
    if (*count == REIST_GUI_PIECE_CAPACITY) return -1;
    pieces[(*count)++] = (reist_gui_piece_t){source, offset, length};
    return 0;
}

int reist_gui_piece_document_insert(reist_gui_piece_document_t *document,
                                    uint32_t offset, const void *data,
                                    uint32_t size) {
    if (!valid(document) || (size != 0U && data == 0) || offset > document->size)
        return REIST_GUI_PIECE_EINVAL;
    if (size == 0U) return 0;
    if (size > REIST_GUI_PIECE_ADDED_CAPACITY - document->added_used ||
        size > UINT32_MAX - document->size) return REIST_GUI_PIECE_ECAPACITY;
    reist_gui_piece_t next[REIST_GUI_PIECE_CAPACITY];
    uint32_t count = 0U, logical = 0U;
    for (uint32_t i = 0U; i < document->piece_count; ++i) {
        reist_gui_piece_t piece = document->pieces[i];
        if (offset >= logical && offset <= logical + piece.length) {
            uint32_t left = offset - logical;
            if (append(next, &count, piece.source, piece.offset, left) != 0 ||
                append(next, &count, REIST_GUI_PIECE_ADDED,
                       document->added_used, size) != 0 ||
                append(next, &count, piece.source, piece.offset + left,
                       piece.length - left) != 0) return REIST_GUI_PIECE_ECAPACITY;
            for (++i; i < document->piece_count; ++i)
                if (append(next, &count, document->pieces[i].source,
                           document->pieces[i].offset,
                           document->pieces[i].length) != 0)
                    return REIST_GUI_PIECE_ECAPACITY;
            bytes_copy(document->added + document->added_used, data, size);
            bytes_copy(document->pieces, next, count * sizeof(next[0]));
            document->piece_count = count; document->added_used += size;
            document->size += size; document->modified = 1U; return 0;
        }
        if (append(next, &count, piece.source, piece.offset, piece.length) != 0)
            return REIST_GUI_PIECE_ECAPACITY;
        logical += piece.length;
    }
    if (offset == document->size &&
        append(next, &count, REIST_GUI_PIECE_ADDED,
               document->added_used, size) == 0) {
        bytes_copy(document->added + document->added_used, data, size);
        bytes_copy(document->pieces, next, count * sizeof(next[0]));
        document->piece_count = count; document->added_used += size;
        document->size += size; document->modified = 1U; return 0;
    }
    return REIST_GUI_PIECE_ECAPACITY;
}

int reist_gui_piece_document_erase(reist_gui_piece_document_t *document,
                                   uint32_t offset, uint32_t size) {
    if (!valid(document) || offset > document->size ||
        size > document->size - offset) return REIST_GUI_PIECE_EINVAL;
    if (size == 0U) return 0;
    reist_gui_piece_t next[REIST_GUI_PIECE_CAPACITY];
    uint32_t count = 0U, logical = 0U, end = offset + size;
    for (uint32_t i = 0U; i < document->piece_count; ++i) {
        reist_gui_piece_t piece = document->pieces[i];
        uint32_t piece_end = logical + piece.length;
        if (logical < offset) {
            uint32_t keep = offset < piece_end ? offset - logical : piece.length;
            if (append(next, &count, piece.source, piece.offset, keep) != 0)
                return REIST_GUI_PIECE_ECAPACITY;
        }
        if (piece_end > end) {
            uint32_t skip = end > logical ? end - logical : 0U;
            if (append(next, &count, piece.source, piece.offset + skip,
                       piece.length - skip) != 0)
                return REIST_GUI_PIECE_ECAPACITY;
        }
        logical = piece_end;
    }
    bytes_copy(document->pieces, next, count * sizeof(next[0]));
    document->piece_count = count; document->size -= size;
    document->modified = 1U; return 0;
}

int reist_gui_piece_document_stream(const reist_gui_piece_document_t *document,
                                    reist_gui_piece_write_fn write_bytes,
                                    void *write_context) {
    if (!valid(document) || write_bytes == 0) return REIST_GUI_PIECE_EINVAL;
    uint8_t buffer[REIST_GUI_PIECE_IO_CAPACITY];
    for (uint32_t i = 0U; i < document->piece_count; ++i) {
        const reist_gui_piece_t *piece = &document->pieces[i];
        uint32_t done = 0U;
        while (done < piece->length) {
            uint32_t amount = piece->length - done;
            if (amount > sizeof(buffer)) amount = sizeof(buffer);
            const void *bytes = document->added + piece->offset + done;
            if (piece->source == REIST_GUI_PIECE_ORIGINAL) {
                if (document->read_original(document->read_context,
                        piece->offset + done, buffer, amount) != 0)
                    return REIST_GUI_PIECE_EIO;
                bytes = buffer;
            }
            if (write_bytes(write_context, bytes, amount) != 0)
                return REIST_GUI_PIECE_EIO;
            done += amount;
        }
    }
    return REIST_GUI_PIECE_OK;
}
