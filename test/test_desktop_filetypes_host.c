#include <assert.h>
#include <string.h>

#include "desktop_filetypes.h"

int main(void) {
    static const char valid[] =
        "# associations\r\n"
        "schema=reist.filetypes/1\r\n"
        ".txt=/usr/gui/bin/notepad.prg\r\n"
        ".conf=/usr/gui/bin/notepad.prg\r\n";
    desktop_filetypes_t table;
    desktop_filetypes_initialize(&table);
    assert(desktop_filetypes_parse(&table, valid, sizeof(valid) - 1U) == 0);
    assert(table.entry_count == 2U);
    const char *program = 0;
    assert(desktop_filetypes_lookup(&table, "/README.TXT", &program) == 0);
    assert(strcmp(program, "/usr/gui/bin/notepad.prg") == 0);
    assert(desktop_filetypes_lookup(&table, "/archive.txt/file", &program) ==
           DESKTOP_FILETYPES_ENOTFOUND);
    assert(desktop_filetypes_lookup(&table, "/bin/demo.prg", &program) ==
           DESKTOP_FILETYPES_ENOTFOUND);

    desktop_filetypes_t snapshot = table;
    static const char duplicate[] =
        "schema=reist.filetypes/1\n"
        ".txt=/usr/gui/bin/notepad.prg\n"
        ".txt=/usr/gui/bin/other.prg\n";
    assert(desktop_filetypes_parse(
        &table, duplicate, sizeof(duplicate) - 1U) < 0);
    assert(memcmp(&table, &snapshot, sizeof(table)) == 0);

    static const char traversal[] =
        "schema=reist.filetypes/1\n.txt=/usr/../bin/edit.prg\n";
    assert(desktop_filetypes_parse(
        &table, traversal, sizeof(traversal) - 1U) < 0);
    return 0;
}
