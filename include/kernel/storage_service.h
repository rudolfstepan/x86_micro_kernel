#ifndef KERNEL_STORAGE_SERVICE_H
#define KERNEL_STORAGE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

bool storage_service_init(void);
bool storage_service_start(uint64_t now_ms);
void storage_service_poll(uint64_t now_ms);
int storage_service_bind(int pid, uint32_t generation);
bool storage_service_authorized(int pid, uint32_t generation);

#endif
