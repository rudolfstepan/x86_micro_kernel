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
bool storage_service_resource_recovering(uint32_t resource);
bool storage_service_media_fingerprint(uint32_t resource, uint32_t *fingerprint);
bool storage_service_expected_fingerprint(uint32_t resource,
                                          uint32_t *fingerprint);
bool storage_service_requalify_media(uint32_t resource,
                                     uint32_t *fingerprint);
bool storage_service_admin_begin(uint32_t resource_mask, bool require_down);
bool storage_service_admin_finish_down(uint32_t resource_mask);
bool storage_service_admin_finish_online(uint32_t resource_mask);
bool storage_service_admin_finish_up(uint32_t resource_mask);
bool storage_service_admin_fail(uint32_t resource_mask);
bool storage_service_resource_admin_down(uint32_t resource);
bool storage_service_resource_admin_transition(uint32_t resource);
bool storage_service_resource_admin_failed(uint32_t resource);
bool storage_service_component_down(uint64_t deadline_ms);
bool storage_service_component_up(uint64_t deadline_ms);
bool storage_service_component_ready(void);

#endif
