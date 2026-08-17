#include "x86os.h"

/* Read-only filesystem consistency probe.  It deliberately uses only the
 * VFS ABI: repair and raw-media access belong to the supervised storage
 * service and are not exposed to an ordinary Ring-3 program. */
#define PATH_CAPACITY 256U
#define READ_CAPACITY 512U
#define MAX_NODES 256U

static unsigned length(const char *text) {
    unsigned value = 0;
    while (text[value] != '\0') ++value;
    return value;
}

static int equal(const char *left, const char *right) {
    while (*left != '\0' && *right != '\0' && *left == *right) {
        ++left; ++right;
    }
    return *left == *right;
}

static int join_path(char *out, const char *parent, const char *name) {
    unsigned at = 0;
    unsigned parent_length = length(parent);
    if (parent_length >= PATH_CAPACITY) return -1;
    for (unsigned i = 0; i < parent_length; ++i) out[at++] = parent[i];
    if (at == 0 || out[at - 1U] != '/') out[at++] = '/';
    for (unsigned i = 0; name[i] != '\0'; ++i) {
        if (at + 1U >= PATH_CAPACITY) return -1;
        out[at++] = name[i];
    }
    out[at] = '\0';
    return 0;
}

static int check_file(const char *path, uint32_t expected) {
    char buffer[READ_CAPACITY];
    int descriptor = x86os_open(path);
    if (descriptor < 0) return -1;
    uint32_t total = 0;
    for (;;) {
        int count = x86os_read(descriptor, buffer, sizeof(buffer));
        if (count < 0 || total > expected || (uint32_t)count > expected - total) {
            (void)x86os_close(descriptor);
            return -1;
        }
        if (count == 0) break;
        total += (uint32_t)count;
    }
    if (x86os_close(descriptor) < 0 || total != expected) return -1;
    return 0;
}

static int scan(const char *path, unsigned *visited, unsigned *errors) {
    x86os_file_info_t info;
    if (*visited >= MAX_NODES || x86os_stat(path, &info) < 0) {
        ++*errors; return -1;
    }
    ++*visited;
    if (info.type == X86OS_FILE) {
        if (check_file(path, info.size) < 0) ++*errors;
        return 0;
    }
    for (uint32_t index = 0;;) {
        x86os_file_info_t entries[X86OS_READDIR_BATCH_CAPACITY];
        int count = x86os_readdir_batch(path, index, entries);
        if (count < 0) { ++*errors; return -1; }
        if (count == 0) break;
        for (int item = 0; item < count; ++item) {
            if (equal(entries[item].name, ".") || equal(entries[item].name, ".."))
                continue;
            char child[PATH_CAPACITY];
            if (join_path(child, path, entries[item].name) < 0 ||
                scan(child, visited, errors) < 0) {
                if (*errors == 0) ++*errors;
            }
        }
        index += (uint32_t)count;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/";
    if (argc > 2 || length(path) >= PATH_CAPACITY) {
        x86os_puts("Usage: chkdsk [path]\n");
        return 2;
    }
    unsigned visited = 0, errors = 0;
    (void)scan(path, &visited, &errors);
    if (errors != 0) {
        x86os_puts("CHKDSK: read-only check failed; medium left unchanged\n");
        return 1;
    }
    x86os_puts("CHKDSK: read-only check passed\n");
    return 0;
}
