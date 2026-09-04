#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "userspace/gui/compositor/desktop_layout.h"

static desktop_layout_identity_t builtin(uint32_t index) {
    desktop_layout_identity_t identity;
    assert(desktop_layout_identity_builtin(&identity, index) ==
           DESKTOP_LAYOUT_OK);
    return identity;
}

static desktop_layout_identity_t file_identity(const char *name) {
    desktop_layout_identity_t identity;
    assert(desktop_layout_identity_file(&identity, name) ==
           DESKTOP_LAYOUT_OK);
    return identity;
}

int main(void) {
    static const char valid[] =
        "schema=reist.desktop-layout/1\n"
        "icon=6275696c74696e3a636f6d7075746572,1,1\n"
        "icon=6465736b746f703a6e6f7465732e747874,0,0\n";
    desktop_layout_document_t document;
    desktop_layout_document_initialize(&document);
    assert(desktop_layout_parse((const uint8_t *)valid,
                                (uint32_t)strlen(valid), &document) ==
           DESKTOP_LAYOUT_OK);
    assert(document.entry_count == 2U);
    assert(strcmp(document.entries[0].identity.value,
                  "builtin:computer") == 0);

    uint8_t serialized[DESKTOP_LAYOUT_FILE_CAPACITY];
    uint32_t serialized_size = 0U;
    assert(desktop_layout_serialize(&document, serialized,
                                    sizeof(serialized),
                                    &serialized_size) == DESKTOP_LAYOUT_OK);
    desktop_layout_document_t round_trip;
    assert(desktop_layout_parse(serialized, serialized_size, &round_trip) ==
           DESKTOP_LAYOUT_OK);
    assert(round_trip.entry_count == document.entry_count);

    static const char duplicate_identity[] =
        "schema=reist.desktop-layout/1\n"
        "icon=6275696c74696e3a636f6d7075746572,0,0\n"
        "icon=6275696c74696e3a636f6d7075746572,1,0\n";
    static const char overlap[] =
        "schema=reist.desktop-layout/1\n"
        "icon=6275696c74696e3a636f6d7075746572,0,0\n"
        "icon=6275696c74696e3a636f6e74726f6c,0,0\n";
    static const char range[] =
        "schema=reist.desktop-layout/1\n"
        "icon=6275696c74696e3a636f6d7075746572,256,0\n";
    static const char trailing[] =
        "schema=reist.desktop-layout/1\ntrailing";
    desktop_layout_document_t unchanged = document;
    assert(desktop_layout_parse((const uint8_t *)duplicate_identity,
                                sizeof(duplicate_identity) - 1U,
                                &unchanged) == DESKTOP_LAYOUT_EMALFORMED);
    assert(memcmp(&unchanged, &document, sizeof(document)) == 0);
    assert(desktop_layout_parse((const uint8_t *)overlap,
                                sizeof(overlap) - 1U,
                                &unchanged) == DESKTOP_LAYOUT_EMALFORMED);
    assert(desktop_layout_parse((const uint8_t *)range,
                                sizeof(range) - 1U,
                                &unchanged) == DESKTOP_LAYOUT_EMALFORMED);
    assert(desktop_layout_parse((const uint8_t *)trailing,
                                sizeof(trailing) - 1U,
                                &unchanged) == DESKTOP_LAYOUT_EMALFORMED);

    desktop_layout_identity_t identities[4] = {
        builtin(DESKTOP_LAYOUT_BUILTIN_COMPUTER),
        builtin(DESKTOP_LAYOUT_BUILTIN_CONTROL),
        builtin(DESKTOP_LAYOUT_BUILTIN_TRASH),
        file_identity("notes.txt"),
    };
    desktop_layout_view_t view;
    desktop_rect_t work = {8, 8, 344U, 136U};
    assert(desktop_layout_resolve(&document, identities, 4U, work, 68U,
                                  7U, &view) == DESKTOP_LAYOUT_OK);
    assert(view.entry_count == 4U && view.generation == 7U);
    for (uint32_t left = 0U; left < view.entry_count; ++left) {
        desktop_rect_t rect = desktop_layout_view_rect(&view, left);
        assert(rect.width != 0U && rect.height == 68U);
        assert(rect.x >= work.x && rect.y >= work.y);
        assert((uint32_t)(rect.x - work.x) + rect.width <= work.width);
        assert((uint32_t)(rect.y - work.y) + rect.height <= work.height);
        for (uint32_t right = left + 1U; right < view.entry_count; ++right)
            assert(view.entries[left].column != view.entries[right].column ||
                   view.entries[left].row != view.entries[right].row);
    }
    assert(document.entries[0].column == 1U &&
           document.entries[0].row == 1U);

    desktop_layout_cell_t cell;
    assert(desktop_layout_drop(&view, 0U, work.x + 1, work.y + 1,
                               &cell) == DESKTOP_LAYOUT_OK);
    for (uint32_t index = 1U; index < view.entry_count; ++index)
        assert(cell.column != view.entries[index].column ||
               cell.row != view.entries[index].row);
    desktop_layout_document_t moved;
    assert(desktop_layout_move_document(&view, 0U, cell, &moved) ==
           DESKTOP_LAYOUT_OK);
    assert(moved.entry_count == view.entry_count);
    assert(desktop_layout_serialize(&moved, serialized, sizeof(serialized),
                                    &serialized_size) == DESKTOP_LAYOUT_OK);
    assert(desktop_layout_parse(serialized, serialized_size, &round_trip) ==
           DESKTOP_LAYOUT_OK);

    desktop_layout_identity_t too_many[DESKTOP_LAYOUT_ENTRY_CAPACITY + 1U];
    assert(desktop_layout_resolve(&document, too_many,
                                  DESKTOP_LAYOUT_ENTRY_CAPACITY + 1U,
                                  work, 68U, 8U, &view) ==
           DESKTOP_LAYOUT_ECAPACITY);
    return 0;
}
