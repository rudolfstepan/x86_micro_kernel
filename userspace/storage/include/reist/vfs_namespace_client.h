/**
 * @file vfs_namespace_client.h
 * @brief Bounded generation-scoped filesystem namespace mutations.
 */
#ifndef REIST_VFS_NAMESPACE_CLIENT_H
#define REIST_VFS_NAMESPACE_CLIENT_H

#include <stdint.h>

#define REIST_VFS_NAMESPACE_DEFAULT_TIMEOUT_MS 5000U
#define REIST_VFS_NAMESPACE_MAX_RECOVERY_RETRIES 1U

/** Remove the final symbolic-link object without following its target. */
int reist_vfs_unlink(const char *path, uint32_t timeout_ms);

/** Rename a final symbolic-link object without replacing an existing target. */
int reist_vfs_rename(const char *source, const char *destination,
                     uint32_t timeout_ms);

#endif
