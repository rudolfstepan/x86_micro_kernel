/**
 * @file test/test_shell_path_host.c
 * @brief Hostseitiger Regressionstest für shell path.
 *
 * Layer: Host test harness.
 * Contract: Prüft beobachtbares Verhalten und feste Fehlergrenzen ohne Zielhardware.
 * Safety: Testdoubles dürfen Produktionsverträge nicht abschwächen oder Erfolg vortäuschen.
 */
#include "kernel/shell/path_resolver.h"

#include <string.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int main(void) {
    char path[SHELL_PATH_MAX];
    char long_path[SHELL_PATH_MAX + 2];
    char selector[SHELL_DRIVE_SELECTOR_MAX];
    const char* remainder = NULL;

    CHECK(shell_path_normalize("/", "README.TXT", path) == SHELL_PATH_OK);
    CHECK(strcmp(path, "/README.TXT") == 0);
    CHECK(shell_path_normalize("/DOCS/SUB", "..\\..\\README.TXT", path) ==
          SHELL_PATH_OK);
    CHECK(strcmp(path, "/README.TXT") == 0);
    CHECK(shell_path_normalize("/DOCS", "/A//./B/../C/", path) ==
          SHELL_PATH_OK);
    CHECK(strcmp(path, "/A/C") == 0);
    CHECK(shell_path_normalize("/", "../../SAFE", path) == SHELL_PATH_OK);
    CHECK(strcmp(path, "/SAFE") == 0);
    CHECK(shell_path_normalize("/DOCS", "", path) == SHELL_PATH_OK);
    CHECK(strcmp(path, "/DOCS") == 0);
    memset(long_path, 'A', sizeof(long_path));
    long_path[sizeof(long_path) - 1] = '\0';
    CHECK(shell_path_normalize("/", long_path, path) ==
          SHELL_PATH_TOO_LONG);

    CHECK(shell_path_split_drive("HDD1:\\TOOLS\\APP.BIN", selector,
                                 &remainder) == SHELL_PATH_OK);
    CHECK(strcmp(selector, "hdd1") == 0);
    CHECK(strcmp(remainder, "\\TOOLS\\APP.BIN") == 0);
    CHECK(shell_path_split_drive("c:\\AUTOEXEC.BAT", selector,
                                 &remainder) == SHELL_PATH_OK);
    CHECK(strcmp(selector, "C") == 0);
    CHECK(strcmp(remainder, "\\AUTOEXEC.BAT") == 0);
    CHECK(shell_path_split_drive("hdd0:", selector, &remainder) ==
          SHELL_PATH_OK);
    CHECK(strcmp(selector, "hdd0") == 0);
    CHECK(strcmp(remainder, "") == 0);
    CHECK(shell_path_split_drive("/FdD0/BOOT", selector, &remainder) ==
          SHELL_PATH_OK);
    CHECK(strcmp(selector, "fdd0") == 0);
    CHECK(strcmp(remainder, "/BOOT") == 0);
    CHECK(shell_path_split_drive("/ordinary/path", selector, &remainder) ==
          SHELL_PATH_OK);
    CHECK(selector[0] == '\0');
    CHECK(strcmp(remainder, "/ordinary/path") == 0);

    CHECK(shell_path_join_mount("/", "/README.TXT", path) == SHELL_PATH_OK);
    CHECK(strcmp(path, "/README.TXT") == 0);
    CHECK(shell_path_join_mount("/mnt/hdd1", "/TOOLS", path) ==
          SHELL_PATH_OK);
    CHECK(strcmp(path, "/mnt/hdd1/TOOLS") == 0);
    CHECK(shell_path_join_mount("/mnt/hdd1", "/", path) == SHELL_PATH_OK);
    CHECK(strcmp(path, "/mnt/hdd1") == 0);

    CHECK(shell_path_to_dos("/TOOLS/BIN", path) == SHELL_PATH_OK);
    CHECK(strcmp(path, "\\TOOLS\\BIN") == 0);
    return 0;
}
