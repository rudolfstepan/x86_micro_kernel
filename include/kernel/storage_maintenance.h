/**
 * @file include/kernel/storage_maintenance.h
 * @brief Administrativer Vertrag für Storage Down/Up, Mount und Medienprüfung.
 *
 * Layer: Ring-0 public subsystem interface.
 * Contract: Anfragen sind ressourcen-, generations- und deadlinegebunden.
 * Safety: Fehler hinterlassen Ressourcen offline, read-only oder quarantänisiert.
 */
#ifndef KERNEL_STORAGE_MAINTENANCE_H
#define KERNEL_STORAGE_MAINTENANCE_H

#include <stdbool.h>
#include <stdint.h>

#define STORAGE_MAINTENANCE_VERSION 1U
#define STORAGE_MAINTENANCE_LEASE_MS 15000U
#define STORAGE_MAINTENANCE_INVALID_TOKEN 0U

typedef uint32_t storage_maintenance_token_t;

bool storage_maintenance_init(void);
int storage_maintenance_acquire(int pid, uint32_t process_generation,
                                uint32_t resource, uint32_t media_fingerprint,
                                uint64_t now_ms,
                                storage_maintenance_token_t *token_out);
int storage_maintenance_renew(int pid, uint32_t process_generation,
                              storage_maintenance_token_t token,
                              uint32_t media_fingerprint, uint64_t now_ms);
int storage_maintenance_release(int pid, uint32_t process_generation,
                                storage_maintenance_token_t token);
bool storage_maintenance_valid(int pid, uint32_t process_generation,
                               storage_maintenance_token_t token,
                               uint32_t media_fingerprint, uint64_t now_ms);
uint32_t storage_maintenance_process_cleanup(int pid,
                                             uint32_t process_generation);

#endif
