#ifndef BROWSER_RESOURCES_H
#define BROWSER_RESOURCES_H
#include "html_protocol.h"
#define BROWSER_RESOURCE_VERSION 2U
#define BROWSER_RESOURCE_COUNT 64U
#define BROWSER_RESOURCE_BYTES (1024U*1024U)
#define BROWSER_RESOURCE_LIMIT (256U*1024U)
#define BROWSER_RESOURCE_DEPTH 8U
#define BROWSER_RESOURCE_DEADLINE_MS 30000U
#define BROWSER_RESOURCE_NEED_MAGIC 0x314e5352U
/* Private, pointer-free immutable snapshots. Each navigation owns one
 * generation; ready entries are a dense prefix with exact byte offsets.
 * No file, socket, allocator or parser authority is implemented here. */
typedef struct browser_resource {
    uint32_t offset, length, ready, depth;
    char url[256], effective[256];
} browser_resource_t;
typedef struct browser_resources {
    uint32_t version, generation, count, length;
    browser_resource_t entries[BROWSER_RESOURCE_COUNT];
    uint8_t bytes[BROWSER_RESOURCE_BYTES];
} browser_resources_t;
#define BROWSER_RESOURCE_PREFIX ((uint32_t)offsetof(browser_resources_t,bytes))
#define BROWSER_RESOURCE_HEADER_BYTES 16U
/* Bundle v2 sends only count metadata records, then exactly length CSS bytes.
 * The fixed-capacity private in-memory representation remains unchanged. */
#define BROWSER_RESOURCE_WIRE_CAPACITY ((uint32_t)sizeof(browser_resources_t))
typedef struct browser_resource_need_item { uint32_t depth; char url[256]; } browser_resource_need_item_t;
typedef struct browser_resource_needs {
    uint32_t magic, version, size, generation;
    browser_html_header_t identity;
    uint32_t count;
    browser_resource_need_item_t items[BROWSER_RESOURCE_COUNT];
} browser_resource_needs_t;
/* RFC 3986 identity: lowercase scheme/host, remove default port and fragment,
 * retain path/query case and query order. Resolver normalizes dot segments. */
int browser_resource_url(const char *base,const char *reference,char out[256]);
int browser_resource_admit(const char *document,const char *url);
void browser_resources_init(browser_resources_t *,uint32_t generation);
int browser_resources_find(const browser_resources_t *,const char *canonical);
int browser_resources_add(browser_resources_t *,const char *document,const char *url,uint32_t depth);
int browser_resources_store(browser_resources_t *,uint32_t index,const char *effective,const uint8_t *,uint32_t length);
int browser_resources_validate(const browser_resources_t *,const char *document,uint32_t generation);
int browser_resources_pack(const browser_resources_t *,const char *document,uint8_t *,uint32_t capacity);
/* Output is private and must be discarded on any unpack/validation error. */
int browser_resources_unpack(const uint8_t *,uint32_t length,const char *document,browser_resources_t *);
int browser_resource_need_add(browser_resource_needs_t *,const char *url,uint32_t depth);
int browser_resource_needs_validate(const browser_resource_needs_t *,uint32_t length,
    const browser_html_header_t *,uint32_t pid,uint32_t child_generation,
    const browser_resources_t *,const char *document);
#endif
