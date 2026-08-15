#ifndef KERNEL_STORAGE_SERVICE_H
#define KERNEL_STORAGE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

bool storage_service_init(void);
bool storage_service_inventory_media(void);
bool storage_service_start(uint64_t now_ms);
void storage_service_poll(uint64_t now_ms);
int storage_service_bind(int pid, uint32_t generation);
bool storage_service_authorized(int pid, uint32_t generation);
bool storage_service_resource_available(uint32_t resource);
bool storage_service_report_io_failure(uint32_t resource);
bool storage_service_report_media_failure(uint32_t resource,
                                          bool write_uncertain);
bool storage_service_resource_read_only(uint32_t resource);
bool storage_service_media_fingerprint(uint32_t resource, uint32_t *fingerprint);

#endif
