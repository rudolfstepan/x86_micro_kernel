/**
 * @file include/kernel/device_domain.h
 * @brief Generic ownership and fencing core for supervised Ring-3 drivers.
 *
 * This layer contains no device-specific state machine.  Immutable platform
 * profiles describe a PCI function and its conservative isolation group;
 * generation-scoped handles then mediate one bounded driver lifecycle.
 */
#ifndef REIST_KERNEL_DEVICE_DOMAIN_H
#define REIST_KERNEL_DEVICE_DOMAIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEVICE_DOMAIN_ABI_VERSION 1U
#define DEVICE_DOMAIN_MAX_DEVICES 16U
#define DEVICE_DOMAIN_MAX_GROUPS 8U
#define DEVICE_DOMAIN_MAX_RESOURCES 128U
#define DEVICE_DOMAIN_INVALID_HANDLE 0U
#define DEVICE_DOMAIN_HANDLE_GENERATION_MAX 0x00FFFFFFU
#define DEVICE_DOMAIN_IRQ_TIMEOUT_MS 250U
#define DEVICE_DOMAIN_IRQ_WINDOW_MS 100U
#define DEVICE_DOMAIN_IRQ_WINDOW_LIMIT 128U
#define DEVICE_DOMAIN_DMA_POOL_COUNT 4U
#define DEVICE_DOMAIN_DMA_POOL_BYTES (64U * 1024U)
#define DEVICE_DOMAIN_DMA_LARGE_POOL_BYTES (512U * 1024U)
#define DEVICE_DOMAIN_DMA_TRANSFER_MAX 1024U
#define DEVICE_DOMAIN_DMA_DESCRIPTOR_BYTES 4096U
#define DEVICE_DOMAIN_DMA_DESCRIPTOR_STRIDE 16U
#define DEVICE_DOMAIN_DMA_DESCRIPTOR_CAPACITY \
    (DEVICE_DOMAIN_DMA_DESCRIPTOR_BYTES / DEVICE_DOMAIN_DMA_DESCRIPTOR_STRIDE)
#define DEVICE_DOMAIN_DMA_DATA_OFFSET DEVICE_DOMAIN_DMA_DESCRIPTOR_BYTES
#define DEVICE_DOMAIN_MAX_REGION_RULES 32U
#define DEVICE_DOMAIN_MAX_REGION_BYTES (8U * 1024U * 1024U)
#define DEVICE_DOMAIN_DMA_ADDRESS_ALIGNMENT 128U

enum {
    DEVICE_DOMAIN_PROFILE_MEDIATED_DMA = 1U << 0U,
    DEVICE_DOMAIN_PROFILE_IOMMU_DIRECT = 1U << 1U,
    DEVICE_DOMAIN_PROFILE_GROUP_ISOLATED = 1U << 2U,
    /** Vetted legacy endpoint may use a line-level PIC mask for INTx. */
    DEVICE_DOMAIN_PROFILE_LEGACY_INTX_PIC = 1U << 3U,
    /** Fixed command mediator; grants neither DMA nor raw region mappings. */
    DEVICE_DOMAIN_PROFILE_MEDIATED_IO = 1U << 4U,
    /** Select the fixed large pool for a vetted mediated-DMA profile. */
    DEVICE_DOMAIN_PROFILE_LARGE_DMA_POOL = 1U << 5U,
};

enum {
    DEVICE_DOMAIN_CONTROL_REGION_OPEN = 1U,
    DEVICE_DOMAIN_CONTROL_IRQ_BIND = 2U,
    DEVICE_DOMAIN_CONTROL_DMA_BIND = 3U,
    DEVICE_DOMAIN_CONTROL_ACTIVATE = 4U,
    DEVICE_DOMAIN_CONTROL_RESOURCE_STATUS = 5U,
    DEVICE_DOMAIN_CONTROL_IOMMU_STATUS = 6U,
    DEVICE_DOMAIN_CONTROL_IRQ_COMPLETE = 7U,
    DEVICE_DOMAIN_CONTROL_DMA_INFO = 8U,
    DEVICE_DOMAIN_CONTROL_DMA_WRITE = 9U,
    DEVICE_DOMAIN_CONTROL_DMA_READ = 10U,
    DEVICE_DOMAIN_CONTROL_DRIVER_BOOTSTRAP = 11U,
    DEVICE_DOMAIN_CONTROL_DRIVER_REPORT = 12U,
    DEVICE_DOMAIN_CONTROL_REGION_READ = 13U,
    DEVICE_DOMAIN_CONTROL_REGION_WRITE = 14U,
    DEVICE_DOMAIN_CONTROL_REGION_BIND_DMA = 15U,
    DEVICE_DOMAIN_CONTROL_DMA_DESCRIPTOR_SET = 16U,
    DEVICE_DOMAIN_CONTROL_DEACTIVATE = 17U,
    DEVICE_DOMAIN_CONTROL_DMA_POOL_STATS = 18U,
};

enum {
    DEVICE_DOMAIN_RESOURCE_REGION = 1U,
    DEVICE_DOMAIN_RESOURCE_IRQ = 2U,
    DEVICE_DOMAIN_RESOURCE_DMA = 3U,
};

enum {
    DEVICE_DOMAIN_DRIVER_REPORT_SELF_TEST = 1U,
    DEVICE_DOMAIN_DRIVER_REPORT_PROGRESS = 2U,
    DEVICE_DOMAIN_DRIVER_REPORT_CHANNEL = 3U,
    DEVICE_DOMAIN_DRIVER_REPORT_DIAGNOSTIC = 4U,
};

enum {
    DEVICE_DOMAIN_REGION_MMIO = 1U << 0U,
    DEVICE_DOMAIN_REGION_PIO = 1U << 1U,
    DEVICE_DOMAIN_REGION_64BIT = 1U << 2U,
    DEVICE_DOMAIN_REGION_PREFETCHABLE = 1U << 3U,
};

enum {
    DEVICE_DOMAIN_REGION_DESCRIBE = 1U << 0U,
    DEVICE_DOMAIN_REGION_MAP_READ = 1U << 1U,
    DEVICE_DOMAIN_REGION_MAP_WRITE = 1U << 2U,
    DEVICE_DOMAIN_REGION_ACCESS_READ = 1U << 3U,
    DEVICE_DOMAIN_REGION_ACCESS_WRITE = 1U << 4U,
};

enum {
    DEVICE_DOMAIN_REGION_RULE_VALUE = 1U,
    DEVICE_DOMAIN_REGION_RULE_DMA_ADDRESS = 2U,
    DEVICE_DOMAIN_REGION_RULE_DMA_DESCRIPTOR_ADDRESS = 3U,
};

enum {
    DEVICE_DOMAIN_MODE_MEDIATED = 1U,
    DEVICE_DOMAIN_MODE_IOMMU_DIRECT = 2U,
};

enum {
    DEVICE_DOMAIN_DMA_TO_DEVICE = 1U << 0U,
    DEVICE_DOMAIN_DMA_FROM_DEVICE = 1U << 1U,
};

enum {
    DEVICE_DOMAIN_AVAILABLE = 1U,
    DEVICE_DOMAIN_CLAIMED = 2U,
    DEVICE_DOMAIN_DMA_BOUND = 3U,
    DEVICE_DOMAIN_ACTIVE = 4U,
    DEVICE_DOMAIN_FENCED = 5U,
    DEVICE_DOMAIN_UNSUPPORTED = 6U,
};

typedef uint32_t device_domain_handle_t;
typedef uint32_t device_domain_resource_handle_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t isolation_group;
    uint32_t flags;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass_code;
    uint8_t prog_if;
    uint8_t reserved_byte;
    uint32_t reserved[3];
} device_domain_profile_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t pci_location;
    uint32_t isolation_group;
    uint32_t state;
    uint32_t generation;
    int32_t owner_pid;
    uint32_t owner_generation;
    uint32_t mode;
    uint32_t irq_bound;
    uint32_t dma_bound;
    uint32_t reserved[3];
} device_domain_status_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t firmware_root_valid;
    uint32_t dmar_present;
    uint32_t dmar_valid;
    uint32_t remapping_unit_count;
    uint32_t interrupt_remapping_reported;
    uint32_t translation_enabled;
    uint32_t direct_assignment_ready;
} device_domain_iommu_status_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    device_domain_handle_t device;
    uint32_t region_index;
    uint32_t rights;
    uint32_t flags;
    uint32_t reserved[2];
} device_domain_region_request_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    device_domain_resource_handle_t resource;
    uint32_t region_index;
    uint32_t flags;
    uint32_t base_low;
    uint32_t base_high;
    uint32_t length_low;
    uint32_t length_high;
    uint32_t rights;
    uint32_t reserved[2];
} device_domain_region_info_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    device_domain_handle_t device;
    uint32_t endpoint_capability;
    uint32_t flags;
    uint32_t reserved[3];
} device_domain_irq_request_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    device_domain_handle_t device;
    uint32_t dma_capability;
    uint32_t flags;
    uint32_t reserved[3];
} device_domain_dma_request_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t kind;
    uint32_t generation;
    device_domain_handle_t device;
    int32_t owner_pid;
    uint32_t owner_generation;
    uint32_t platform_capability;
    uint32_t reserved[2];
} device_domain_resource_status_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    device_domain_handle_t device;
    uint32_t flags;
    uint32_t reserved[4];
} device_domain_action_request_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    device_domain_resource_handle_t resource;
    uint32_t flags;
    uint32_t reserved[4];
} device_domain_resource_request_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    device_domain_resource_handle_t resource;
    uint32_t kind;
    uint32_t reserved[4];
} device_domain_resource_result_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    device_domain_resource_handle_t resource;
    uint32_t sequence;
    uint32_t pending_count;
    uint32_t reserved[3];
} device_domain_irq_message_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    device_domain_resource_handle_t resource;
    uint32_t sequence;
    uint32_t completed_count;
    uint32_t reserved[3];
} device_domain_irq_completion_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    device_domain_resource_handle_t resource;
    uint32_t offset;
    uint32_t length;
    uint32_t user_buffer;
    uint32_t flags;
    uint32_t reserved;
} device_domain_dma_transfer_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    device_domain_resource_handle_t resource;
    uint32_t capacity;
    uint32_t alignment;
    uint32_t direction;
    uint32_t reserved[2];
} device_domain_dma_info_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t active_pools;
    uint32_t peak_active_pools;
    uint32_t capacity;
    uint32_t capacity_rejections;
    uint32_t pool_bytes;
    uint32_t reserved;
} device_domain_dma_pool_stats_t;

enum {
    DEVICE_DOMAIN_DMA_DESCRIPTOR_INTERRUPT = 1U << 0U,
};

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    device_domain_resource_handle_t dma;
    uint32_t descriptor_index;
    uint32_t buffer_offset;
    uint32_t length;
    uint32_t flags;
    uint32_t reserved;
} device_domain_dma_descriptor_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    device_domain_handle_t device;
    uint32_t mode;
    uint32_t session_slot;
    uint32_t session_generation;
    uint32_t session_epoch;
    uint32_t reserved;
} device_domain_driver_bootstrap_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t session_slot;
    uint32_t session_generation;
    uint32_t session_epoch;
    uint32_t report_type;
    uint32_t value;
    uint32_t flags;
} device_domain_driver_report_t;

typedef struct {
    uint32_t region_index;
    uint32_t offset;
    uint32_t width;
    uint32_t kind;
    uint32_t writable_mask;
    uint32_t reserved;
} device_domain_region_rule_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t readable_bytes[6];
    uint32_t rule_count;
    uint32_t reserved;
    device_domain_region_rule_t rules[DEVICE_DOMAIN_MAX_REGION_RULES];
} device_domain_region_policy_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    device_domain_resource_handle_t region;
    uint32_t offset;
    uint32_t width;
    uint32_t value;
    uint32_t flags;
    uint32_t reserved;
} device_domain_region_access_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    device_domain_resource_handle_t region;
    uint32_t offset;
    uint32_t width;
    uint32_t value;
    uint32_t reserved[2];
} device_domain_region_value_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    device_domain_resource_handle_t region;
    device_domain_resource_handle_t dma;
    uint32_t register_offset;
    uint32_t buffer_offset;
    uint32_t flags;
    uint32_t reserved;
} device_domain_region_dma_address_t;

typedef struct {
    uint64_t (*monotonic_ms)(void);
    bool (*claim_device)(uint32_t pci_location,
                         const device_domain_profile_t *profile);
    bool (*set_bus_master)(uint32_t pci_location, bool enabled);
    bool (*mask_irq)(uint32_t pci_location);
    bool (*unmask_irq)(uint32_t pci_location);
    bool (*describe_region)(uint32_t pci_location, uint32_t region_index,
                            device_domain_region_info_t *region);
    bool (*prepare_region)(const device_domain_region_info_t *region);
    bool (*read_region)(const device_domain_region_info_t *region,
                        uint32_t offset, uint32_t width, uint32_t *value);
    bool (*write_region)(const device_domain_region_info_t *region,
                         uint32_t offset, uint32_t width, uint32_t value);
    bool (*write_dma_address)(const device_domain_region_info_t *region,
                              uint32_t offset, uint32_t address_low,
                              uint32_t address_high);
    bool (*bind_irq)(uint32_t pci_location, int pid,
                     uint32_t process_generation, uint32_t capability);
    bool (*revoke_irq)(uint32_t pci_location, int pid,
                       uint32_t process_generation, uint32_t capability);
    bool (*bind_dma)(uint32_t pci_location, int pid,
                     uint32_t process_generation, uint32_t mode,
                     uint32_t capability);
    bool (*revoke_dma)(uint32_t pci_location, int pid,
                       uint32_t process_generation, uint32_t capability);
    bool (*reset)(uint32_t pci_location, uint64_t deadline_ms);
} device_domain_platform_ops_t;

_Static_assert(sizeof(device_domain_profile_t) == 36U,
               "device-domain profile ABI changed");
_Static_assert(sizeof(device_domain_status_t) == 56U,
               "device-domain status ABI changed");
_Static_assert(sizeof(device_domain_iommu_status_t) == 36U,
               "device-domain IOMMU status ABI changed");
_Static_assert(sizeof(device_domain_region_request_t) == 32U,
               "device-domain region request ABI changed");
_Static_assert(sizeof(device_domain_region_info_t) == 48U,
               "device-domain region info ABI changed");
_Static_assert(sizeof(device_domain_irq_request_t) == 32U,
               "device-domain IRQ request ABI changed");
_Static_assert(sizeof(device_domain_dma_request_t) == 32U,
               "device-domain DMA request ABI changed");
_Static_assert(sizeof(device_domain_resource_status_t) == 40U,
               "device-domain resource status ABI changed");
_Static_assert(sizeof(device_domain_action_request_t) == 32U,
               "device-domain action request ABI changed");
_Static_assert(sizeof(device_domain_resource_request_t) == 32U,
               "device-domain resource request ABI changed");
_Static_assert(sizeof(device_domain_resource_result_t) == 32U,
               "device-domain resource result ABI changed");
_Static_assert(sizeof(device_domain_irq_message_t) == 32U,
               "device-domain IRQ message ABI changed");
_Static_assert(sizeof(device_domain_irq_completion_t) == 32U,
               "device-domain IRQ completion ABI changed");
_Static_assert(sizeof(device_domain_dma_transfer_t) == 32U,
               "device-domain DMA transfer ABI changed");
_Static_assert(sizeof(device_domain_dma_info_t) == 32U,
               "device-domain DMA info ABI changed");
_Static_assert(sizeof(device_domain_dma_pool_stats_t) == 32U,
               "device-domain DMA pool statistics ABI changed");
_Static_assert(sizeof(device_domain_driver_bootstrap_t) == 32U,
               "device-domain driver bootstrap ABI changed");
_Static_assert(sizeof(device_domain_driver_report_t) == 32U,
               "device-domain driver report ABI changed");
_Static_assert(sizeof(device_domain_dma_descriptor_t) == 32U,
               "device-domain DMA descriptor ABI changed");
_Static_assert(sizeof(device_domain_region_rule_t) == 24U,
               "device-domain region rule ABI changed");
_Static_assert(sizeof(device_domain_region_policy_t) == 808U,
               "device-domain region policy ABI changed");
_Static_assert(sizeof(device_domain_region_access_t) == 32U,
               "device-domain region access ABI changed");
_Static_assert(sizeof(device_domain_region_value_t) == 32U,
               "device-domain region value ABI changed");
_Static_assert(sizeof(device_domain_region_dma_address_t) == 32U,
               "device-domain region DMA address ABI changed");

/** Reset all fixed tables. No registered device survives initialization. */
bool device_domain_init(const device_domain_platform_ops_t *ops,
                        bool iommu_ready);
/** Install the current fail-closed PCI backend; no DMA assignment is implied. */
bool device_domain_bootstrap(void);
#ifdef REIST_DRIVER_DOMAIN_FAULT_INJECTION
/** Register two synthetic functions used only by the bounded QEMU fault gate. */
int device_domain_fault_test_register(uint32_t *recovery_device_out,
                                      uint32_t *reset_device_out);
#endif
/**
 * Register one exact function from immutable platform inventory.
 * device_out remains a valid diagnostic index when initial fencing returns
 * EIO; that slot is permanently UNSUPPORTED and cannot be claimed.
 */
int device_domain_register(const device_domain_profile_t *profile,
                           uint32_t pci_location, uint32_t *device_out);
/** Install immutable register-safety metadata before the first claim. */
int device_domain_install_region_policy(
    uint32_t device, const device_domain_region_policy_t *policy);
/** Claim one function and its complete isolation group for a process generation. */
int device_domain_claim(int pid, uint32_t process_generation, uint32_t device,
                        uint32_t mode, device_domain_handle_t *handle_out);
/** Describe one declared BAR as a separate capability; this does not map it. */
int device_domain_open_region(int pid, uint32_t process_generation,
                              const device_domain_region_request_t *request,
                              device_domain_region_info_t *region_out);
/** Bind the driver's bounded IRQ endpoint as a separate capability. */
int device_domain_bind_irq(int pid, uint32_t process_generation,
                           const device_domain_irq_request_t *request,
                           device_domain_resource_handle_t *resource_out);
/** Bind a validated mediated pool or IOMMU space as a separate capability. */
int device_domain_bind_dma(int pid, uint32_t process_generation,
                           const device_domain_dma_request_t *request,
                           device_domain_resource_handle_t *resource_out);
/** Enable device production only after IRQ and DMA ownership are complete. */
int device_domain_activate(int pid, uint32_t process_generation,
                           device_domain_handle_t handle);
/**
 * Mask IRQ and disable bus mastering while retaining generation-scoped
 * resources so a mediated DMA pool can be safely refilled.
 */
int device_domain_deactivate(int pid, uint32_t process_generation,
                             device_domain_handle_t handle);
/** Mask IRQ and disable bus mastering before any owner cleanup. */
int device_domain_fence(int pid, uint32_t process_generation,
                        device_domain_handle_t handle);
/** Record completion of a kernel-owned fixed-command I/O quiesce. */
int device_domain_mark_mediated_io_quiesced(
    int pid, uint32_t process_generation, device_domain_handle_t handle);
/** Fence, reset and release one device before publishing a new generation. */
int device_domain_release(int pid, uint32_t process_generation,
                          device_domain_handle_t handle,
                          uint64_t deadline_ms);
/** Immediately fence resources owned by a dead process; reset stays supervised. */
void device_domain_process_cleanup(int pid, uint32_t process_generation);
/**
 * Recover every device held by one dead process generation as one transaction.
 * Ownership and handle generations are published only after all devices are
 * fenced and reset before the supplied monotonic deadline.
 */
int device_domain_recover_owner(int pid, uint32_t process_generation,
                                uint64_t deadline_ms);
int device_domain_status(uint32_t device, device_domain_status_t *status);
/** Return discovery and activation separately; discovery never grants DMA. */
int device_domain_iommu_status(device_domain_iommu_status_t *status);
int device_domain_resource_status(int pid, uint32_t process_generation,
                                  device_domain_resource_handle_t resource,
                                  device_domain_resource_status_t *status);
int device_domain_irq_complete(int pid, uint32_t process_generation,
                               device_domain_resource_handle_t resource,
                               device_domain_irq_completion_t *completion);
/** Return public bounds for a kernel-owned mediated DMA pool. */
int device_domain_dma_info(int pid, uint32_t process_generation,
                           device_domain_resource_handle_t resource,
                           device_domain_dma_info_t *info);
/** Return aggregate bounded-pool pressure without exposing DMA authority. */
int device_domain_dma_pool_stats(device_domain_dma_pool_stats_t *stats);
/** Copy one bounded block into a TO_DEVICE mediated DMA pool. */
int device_domain_dma_write(int pid, uint32_t process_generation,
                            device_domain_resource_handle_t resource,
                            uint32_t offset, const void *data, uint32_t length);
/** Copy one bounded block out of a FROM_DEVICE mediated DMA pool. */
int device_domain_dma_read(int pid, uint32_t process_generation,
                           device_domain_resource_handle_t resource,
                           uint32_t offset, void *data, uint32_t length);
/** Construct one sealed 64-bit-address/32-bit-length/32-bit-flags entry. */
int device_domain_dma_descriptor_set(
    int pid, uint32_t process_generation,
    const device_domain_dma_descriptor_t *request);
int device_domain_region_read(int pid, uint32_t process_generation,
                              const device_domain_region_access_t *request,
                              device_domain_region_value_t *result);
int device_domain_region_write(int pid, uint32_t process_generation,
                               const device_domain_region_access_t *request);
int device_domain_region_bind_dma(
    int pid, uint32_t process_generation,
    const device_domain_region_dma_address_t *request);
/** Deliver deferred bounded IRQ notifications and enforce their deadlines. */
void device_domain_poll(uint64_t now_ms);

#ifdef REIST_RUNTIME_DEGRADATION_FAULT_INJECTION
bool device_domain_irq_storm_self_test(void);
#endif

#ifdef REIST_HOST_TEST
void device_domain_test_reset(void);
void device_domain_test_raise_irq(uint8_t irq);
#endif

#endif
