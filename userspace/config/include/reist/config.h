/**
 * @file reist/config.h
 * @brief Fixed-capacity parser and serializer for reist.config/1 files.
 *
 * Parsing is transactional from the caller's perspective: an error clears the
 * destination document. The library performs no allocation or file I/O.
 */
#ifndef REIST_CONFIG_H
#define REIST_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REIST_CONFIG_FILE_CAPACITY 4096U
#define REIST_CONFIG_LINE_CAPACITY 160U
#define REIST_CONFIG_SCHEMA_CAPACITY 40U
#define REIST_CONFIG_ENTRY_CAPACITY 32U
#define REIST_CONFIG_KEY_CAPACITY 48U
#define REIST_CONFIG_VALUE_CAPACITY 96U

enum reist_config_status {
    REIST_CONFIG_OK = 0,
    REIST_CONFIG_EINVAL = -22,
    REIST_CONFIG_ECAPACITY = -75,
    REIST_CONFIG_ESCHEMA = -71,
    REIST_CONFIG_EDUPLICATE = -17
};

typedef struct reist_config_entry {
    char key[REIST_CONFIG_KEY_CAPACITY];
    char value[REIST_CONFIG_VALUE_CAPACITY];
} reist_config_entry_t;

typedef struct reist_config_document {
    char schema[REIST_CONFIG_SCHEMA_CAPACITY];
    reist_config_entry_t entries[REIST_CONFIG_ENTRY_CAPACITY];
    uint32_t entry_count;
} reist_config_document_t;

void reist_config_initialize(reist_config_document_t *document);
int reist_config_parse(const char *data, size_t length,
                       const char *expected_schema,
                       reist_config_document_t *document);
const char *reist_config_get(const reist_config_document_t *document,
                             const char *key);
int reist_config_set(reist_config_document_t *document, const char *key,
                     const char *value);
int reist_config_serialize(const reist_config_document_t *document,
                           char *output, size_t capacity,
                           size_t *length_out);

#ifdef __cplusplus
}
#endif

#endif
