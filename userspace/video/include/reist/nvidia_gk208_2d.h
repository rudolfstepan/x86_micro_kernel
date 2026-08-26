/**
 * @file nvidia_gk208_2d.h
 * @brief Fixed FERMI_TWOD_A command contract for the exact GK208 target.
 *
 * This compiler does not submit work.  It produces one bounded, validated
 * method stream for a later kernel-owned GPFIFO mediator.
 */
#ifndef REIST_VIDEO_NVIDIA_GK208_2D_H
#define REIST_VIDEO_NVIDIA_GK208_2D_H

#include <stdint.h>

#define REIST_NVIDIA_GK208_FERMI_TWOD_A 0x0000902DU
#define REIST_NVIDIA_GK208_PUSHBUF_WORD_CAPACITY 64U
#define REIST_NVIDIA_GK208_SUBMISSION_WORD_CAPACITY 72U
#define REIST_NVIDIA_GK208_GPFIFO_ENTRY_WORDS 2U
#define REIST_NVIDIA_GK208_2D_SUBCHANNEL 3U
#define REIST_NVIDIA_GK208_SURFACE_ALIGNMENT 256U
#define REIST_NVIDIA_GK208_MAX_DIMENSION 4096U
#define REIST_NVIDIA_GK208_MAX_PITCH 65536U
#define REIST_NVIDIA_GK208_DMA_POOL_BYTES (512U * 1024U)
#define REIST_NVIDIA_GK208_DMA_DESCRIPTOR_BYTES 4096U
#define REIST_NVIDIA_GK208_DMA_GPFIFO_OFFSET 0x00001000U
#define REIST_NVIDIA_GK208_DMA_PUSHBUF_OFFSET 0x00002000U
#define REIST_NVIDIA_GK208_DMA_FENCE_OFFSET 0x00003000U
#define REIST_NVIDIA_GK208_DMA_USERD_OFFSET 0x00004000U
#define REIST_NVIDIA_GK208_DMA_RAMFC_OFFSET 0x00005000U
#define REIST_NVIDIA_GK208_DMA_RUNLIST_OFFSET 0x00006000U
#define REIST_NVIDIA_GK208_DMA_PGD_OFFSET 0x00010000U
#define REIST_NVIDIA_GK208_DMA_PGD_RESERVATION_BYTES 0x00020000U
#define REIST_NVIDIA_GK208_DMA_PGT_OFFSET 0x00030000U
#define REIST_NVIDIA_GK208_DMA_PGT_RESERVATION_BYTES 0x00040000U
#define REIST_NVIDIA_GK208_DMA_FECS_DATA_OFFSET 0x00070000U
#define REIST_NVIDIA_GK208_DMA_FECS_CODE_OFFSET 0x00070400U
#define REIST_NVIDIA_GK208_DMA_GPCCS_DATA_OFFSET 0x00071000U
#define REIST_NVIDIA_GK208_DMA_GPCCS_CODE_OFFSET 0x00071400U
#define REIST_NVIDIA_GK208_DMA_GR_EXECUTION_OFFSET 0x00072000U
#define REIST_NVIDIA_GK208_GR_FIRMWARE_POLICY_ID 1U
#define REIST_NVIDIA_GK208_PUSHBUF_GPU_ADDRESS 0x0000000020000000ULL
#define REIST_NVIDIA_GK208_FENCE_GPU_ADDRESS 0x0000000020001000ULL
#define REIST_NVIDIA_GK208_GPFIFO_GPU_ADDRESS 0x0000000020002000ULL
#define REIST_NVIDIA_GK208_GPFIFO_BYTES 0x00001000U
#define REIST_NVIDIA_GK208_RAMFC_BYTES 0x00001000U
#define REIST_NVIDIA_GK208_USERD_BYTES 0x00000200U
#define REIST_NVIDIA_GK208_RUNLIST_BYTES 8U
#define REIST_NVIDIA_GK208_CHANNEL_ID 1U
#define REIST_NVIDIA_GK208_CHANNEL_LIMIT 1024U
#define REIST_NVIDIA_GK208_GR_DEVICE_MASK 1U
#define REIST_NVIDIA_GK208_ADDRESS_RELOCATION_WIDTH 8U
#define REIST_NVIDIA_GK208_VM_RELOCATION_COUNT 5U
#define REIST_NVIDIA_GK208_GPU_PAGE_SHIFT 12U
#define REIST_NVIDIA_GK208_FB_PAGE_SHIFT_64K 16U
#define REIST_NVIDIA_GK208_FB_PAGE_SHIFT_128K 17U
#define REIST_NVIDIA_GK208_DEFAULT_FB_PAGE_SHIFT \
    REIST_NVIDIA_GK208_FB_PAGE_SHIFT_128K
#define REIST_NVIDIA_GK208_SEAL_RELOCATION_COUNT 6U
#define REIST_NVIDIA_GK208_VM_ADDRESS_BITS 40U
#define REIST_NVIDIA_GK208_VM_LIMIT 0x0000010000000000ULL
#define REIST_NVIDIA_GK208_RAMFC_PGD_OFFSET 0x00000200U
#define REIST_NVIDIA_GK208_RAMFC_VM_LIMIT_OFFSET 0x00000208U
#define REIST_NVIDIA_GK208_RAMFC_WORDS \
    (REIST_NVIDIA_GK208_RAMFC_BYTES / sizeof(uint32_t))
#define REIST_NVIDIA_GK208_USERD_WORDS \
    (REIST_NVIDIA_GK208_USERD_BYTES / sizeof(uint32_t))
#define REIST_NVIDIA_GK208_RUNLIST_WORDS \
    (REIST_NVIDIA_GK208_RUNLIST_BYTES / sizeof(uint32_t))
#define REIST_NVIDIA_GK208_GR_FIRMWARE_MANIFEST_VERSION 1U
#define REIST_NVIDIA_GK208_GR_FECS_DATA_WORDS 193U
#define REIST_NVIDIA_GK208_GR_FECS_CODE_WORDS 640U
#define REIST_NVIDIA_GK208_GR_GPCCS_DATA_WORDS 27U
#define REIST_NVIDIA_GK208_GR_GPCCS_CODE_WORDS 384U
#define REIST_NVIDIA_GK208_GR_FECS_DATA_CRC32 0x599287F1U
#define REIST_NVIDIA_GK208_GR_FECS_CODE_CRC32 0x761F1915U
#define REIST_NVIDIA_GK208_GR_GPCCS_DATA_CRC32 0xF7976F94U
#define REIST_NVIDIA_GK208_GR_GPCCS_CODE_CRC32 0xF70A347FU
#define REIST_NVIDIA_GK208_GR_FIRMWARE_TOTAL_WORDS 1244U
#define REIST_NVIDIA_GK208_GR_PLAN_VERSION 1U
#define REIST_NVIDIA_GK208_GR_MMIO_PACK_COUNT 30U
#define REIST_NVIDIA_GK208_GR_CONTEXT_PACK_COUNT 5U
#define REIST_NVIDIA_GK208_GR_CONTEXT_TRANSFER_CAPACITY 256U
#define REIST_NVIDIA_GK208_GR_EXECUTION_VERSION 1U
#define REIST_NVIDIA_GK208_GR_EXECUTION_HEADER_BYTES 64U
#define REIST_NVIDIA_GK208_GR_EXECUTION_OP_CAPACITY 2048U
#define REIST_NVIDIA_GK208_GR_EXECUTION_OP_BYTES 16U
#define REIST_NVIDIA_GK208_GR_EXECUTION_MAX_BYTES \
    (REIST_NVIDIA_GK208_GR_EXECUTION_HEADER_BYTES + \
     REIST_NVIDIA_GK208_GR_EXECUTION_OP_CAPACITY * \
         REIST_NVIDIA_GK208_GR_EXECUTION_OP_BYTES)
#define REIST_NVIDIA_GK208_GR_EXECUTION_FLAG_HARDWARE_INACTIVE 0x00000001U
#define REIST_NVIDIA_GK208_GR_EXECUTION_FLAG_NOFW 0x00000002U
#define REIST_NVIDIA_GK208_GR_EXECUTION_FLAG_UNRESOLVED_VRAM 0x00000004U
#define REIST_NVIDIA_GK208_GR_VRAM_RELOCATION_COUNT 2U
#define REIST_NVIDIA_GK208_GR_VRAM_BUFFER_BYTES 0x00020000U
#define REIST_NVIDIA_GK208_GR_VRAM_BUFFER_ALIGNMENT 0x00020000U
#define REIST_NVIDIA_GK208_GR_VRAM_BUFFER_MMU_WRITE 1U
#define REIST_NVIDIA_GK208_GR_VRAM_BUFFER_MMU_READ 2U
#define REIST_NVIDIA_GK208_GR_VRAM_ADDRESS_SHIFT 8U
#define REIST_NVIDIA_GK208_GR_IDLE_DEADLINE_MS 2000U
#define REIST_NVIDIA_GK208_GR_PAGEPOOL_BYTES 0x00008000U
#define REIST_NVIDIA_GK208_GR_PAGEPOOL_ALIGNMENT 0x00000100U
#define REIST_NVIDIA_GK208_GR_BUNDLE_BYTES 0x00003000U
#define REIST_NVIDIA_GK208_GR_BUNDLE_ALIGNMENT 0x00000100U
#define REIST_NVIDIA_GK208_GR_ATTRIB_ALIGNMENT 0x00001000U
#define REIST_NVIDIA_GK208_GR_ATTRIB_STRIDE 0x00000020U
#define REIST_NVIDIA_GK208_GR_ATTRIB_NR_MAX 0x00000324U
#define REIST_NVIDIA_GK208_GR_ALPHA_NR_MAX 0x000007FFU
#define REIST_NVIDIA_GK208_GR_GOLDEN_CB_RESERVED 0x00080000U
#define REIST_NVIDIA_GK208_GR_GOLDEN_ALIGNMENT 0x00001000U
#define REIST_NVIDIA_GK208_GR_CONTEXT_MEMORY_PLAN_VERSION 1U
#define REIST_NVIDIA_GK208_MAX_GPCS 32U
#define REIST_NVIDIA_GK208_MAX_TPCS_PER_GPC 8U
#define REIST_NVIDIA_GK208_MAX_TOTAL_TPCS 32U
#define REIST_NVIDIA_GK208_MAX_ROPS 31U
#define REIST_NVIDIA_GK208_GPC_UNIT_BASE 0x00500000U
#define REIST_NVIDIA_GK208_GPC_UNIT_STRIDE 0x00008000U
#define REIST_NVIDIA_GK208_GPC_TPC_COUNT_OFFSET 0x00002608U
#define REIST_NVIDIA_GK208_GPC_PPC_MASK_OFFSET 0x00000C30U
#define REIST_NVIDIA_GK208_BAR0_TOPOLOGY_BYTES 0x005FA60CU

enum {
    REIST_NVIDIA_GK208_GR_COMPONENT_FECS = 1U,
    REIST_NVIDIA_GK208_GR_COMPONENT_GPCCS = 2U,
};

enum {
    REIST_NVIDIA_GK208_GR_SECTION_DATA = 1U,
    REIST_NVIDIA_GK208_GR_SECTION_CODE = 2U,
};

enum {
    REIST_NVIDIA_GK208_GR_OP_WRITE32 = 1U,
    REIST_NVIDIA_GK208_GR_OP_MASK32 = 2U,
    REIST_NVIDIA_GK208_GR_OP_COPY_MASKED32 = 3U,
    REIST_NVIDIA_GK208_GR_OP_VRAM_OFFSET32 = 4U,
    REIST_NVIDIA_GK208_GR_OP_WAIT_IDLE = 5U,
    REIST_NVIDIA_GK208_GR_OP_CONTEXT_GROUP = 6U,
    REIST_NVIDIA_GK208_GR_OP_CONTEXT_TRANSFER = 7U,
    REIST_NVIDIA_GK208_GR_OP_WAIT_MASK32 = 8U,
    REIST_NVIDIA_GK208_GR_OP_READ32_NONZERO = 9U,
};

typedef struct {
    uint64_t gpu_address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
} reist_nvidia_gk208_surface_t;

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} reist_nvidia_gk208_rect_t;

typedef struct {
    uint32_t words[REIST_NVIDIA_GK208_PUSHBUF_WORD_CAPACITY];
    uint32_t word_count;
} reist_nvidia_gk208_pushbuf_t;

typedef struct {
    uint32_t words[REIST_NVIDIA_GK208_SUBMISSION_WORD_CAPACITY];
    uint32_t word_count;
    uint32_t gpfifo_entry[REIST_NVIDIA_GK208_GPFIFO_ENTRY_WORDS];
} reist_nvidia_gk208_submission_t;

typedef struct {
    uint32_t gpfifo_offset;
    uint32_t gpfifo_bytes;
    uint32_t pushbuf_offset;
    uint32_t pushbuf_bytes;
    uint32_t fence_offset;
    uint32_t fence_bytes;
    uint32_t fence_sequence;
    uint32_t reserved;
} reist_nvidia_gk208_dma_staging_t;

typedef struct {
    uint32_t destination_pool_offset;
    uint32_t source_pool_offset;
    uint32_t width;
    uint32_t reserved;
} reist_nvidia_gk208_address_relocation_t;

typedef struct {
    uint32_t ramfc[REIST_NVIDIA_GK208_RAMFC_WORDS];
    uint32_t userd[REIST_NVIDIA_GK208_USERD_WORDS];
    uint32_t runlist[REIST_NVIDIA_GK208_RUNLIST_WORDS];
    uint32_t ramfc_pool_offset;
    uint32_t ramfc_bytes;
    uint32_t userd_pool_offset;
    uint32_t userd_bytes;
    uint32_t runlist_pool_offset;
    uint32_t runlist_bytes;
    uint32_t channel_id;
    uint32_t gpfifo_bytes;
    reist_nvidia_gk208_address_relocation_t userd_relocation;
} reist_nvidia_gk208_channel_image_t;

typedef struct {
    uint32_t destination_pool_offset;
    uint32_t source_pool_offset;
    uint32_t shift_right;
    uint32_t width;
    uint64_t fixed_bits;
} reist_nvidia_gk208_vm_relocation_t;

typedef struct {
    uint32_t fb_page_shift;
    uint32_t gpu_page_shift;
    uint32_t pgd_bits;
    uint32_t pgt_bits;
    uint32_t pgd_pool_offset;
    uint32_t pgd_bytes;
    uint32_t pgt_pool_offset;
    uint32_t pgt_bytes;
    uint64_t vm_limit;
    uint32_t relocation_count;
    uint32_t reserved;
    reist_nvidia_gk208_vm_relocation_t
        relocations[REIST_NVIDIA_GK208_VM_RELOCATION_COUNT];
} reist_nvidia_gk208_vm_plan_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t fecs_data_words;
    uint32_t fecs_code_words;
    uint32_t gpccs_data_words;
    uint32_t gpccs_code_words;
    uint32_t fecs_data_crc32;
    uint32_t fecs_code_crc32;
    uint32_t gpccs_data_crc32;
    uint32_t gpccs_code_crc32;
    uint32_t total_words;
    uint32_t reserved[5];
} reist_nvidia_gk208_gr_firmware_manifest_t;

typedef struct {
    uint32_t address;
    uint32_t count;
    uint32_t pitch;
    uint32_t value;
} reist_nvidia_gk208_gr_tuple_t;

typedef struct {
    uint32_t first_tuple;
    uint32_t tuple_count;
} reist_nvidia_gk208_gr_span_t;

typedef struct {
    uint32_t first_tuple;
    uint32_t tuple_count;
    uint32_t falcon_base;
    uint32_t starstar;
    uint32_t register_base;
} reist_nvidia_gk208_gr_context_span_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t gpc_count;
    uint32_t rop_count;
    uint32_t tpc_total;
    uint32_t tpc_max;
    uint32_t tpc_count[REIST_NVIDIA_GK208_MAX_GPCS];
    uint32_t ppc_tpc_mask[REIST_NVIDIA_GK208_MAX_GPCS];
    uint32_t reserved[2];
} reist_nvidia_gk208_gr_topology_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t mmio_pack_count;
    uint32_t mmio_tuple_count;
    uint32_t mmio_crc32;
    uint32_t context_pack_count;
    uint32_t context_tuple_count;
    uint32_t context_crc32;
    uint32_t hub_command_offset;
    uint32_t hub_command_value;
    uint32_t hub_start_offset;
    uint32_t hub_start_value;
    uint32_t ready_offset;
    uint32_t ready_mask;
    uint32_t context_size_offset;
    uint32_t ready_deadline_ms;
} reist_nvidia_gk208_gr_plan_manifest_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t word_count;
    uint32_t group_first[REIST_NVIDIA_GK208_GR_CONTEXT_PACK_COUNT];
    uint32_t group_count[REIST_NVIDIA_GK208_GR_CONTEXT_PACK_COUNT];
    uint32_t words[REIST_NVIDIA_GK208_GR_CONTEXT_TRANSFER_CAPACITY];
} reist_nvidia_gk208_gr_context_plan_t;

typedef struct {
    uint32_t opcode;
    uint32_t address;
    uint32_t value;
    uint32_t mask;
} reist_nvidia_gk208_gr_execution_op_t;

typedef struct {
    uint32_t version;
    uint32_t header_size;
    uint32_t used_bytes;
    uint32_t operation_count;
    uint32_t operation_crc32;
    uint32_t topology_crc32;
    uint32_t static_mmio_operation_count;
    uint32_t zbc_operation_count;
    uint32_t context_operation_count;
    uint32_t vram_relocation_count;
    uint32_t flags;
    uint32_t gpc_count;
    uint32_t tpc_total;
    uint32_t rop_count;
    uint32_t reserved[2];
} reist_nvidia_gk208_gr_execution_header_t;

typedef struct {
    reist_nvidia_gk208_gr_execution_header_t header;
    reist_nvidia_gk208_gr_execution_op_t
        operations[REIST_NVIDIA_GK208_GR_EXECUTION_OP_CAPACITY];
} reist_nvidia_gk208_gr_execution_image_t;

typedef struct {
    uint32_t version;
    uint32_t struct_size;
    uint32_t topology_crc32;
    uint32_t tpc_total;
    uint32_t context_size;
    uint32_t pagepool_bytes;
    uint32_t pagepool_alignment;
    uint32_t bundle_bytes;
    uint32_t bundle_alignment;
    uint32_t attrib_bytes;
    uint32_t attrib_alignment;
    uint32_t golden_cb_reserved;
    uint32_t golden_bytes;
    uint32_t golden_alignment;
    uint32_t total_bytes;
    uint32_t flags;
} reist_nvidia_gk208_gr_context_memory_plan_t;

int reist_nvidia_gk208_encode_fill(
    reist_nvidia_gk208_pushbuf_t *pushbuf,
    const reist_nvidia_gk208_surface_t *surface,
    const reist_nvidia_gk208_rect_t *destination,
    uint32_t xrgb8888);
int reist_nvidia_gk208_encode_copy(
    reist_nvidia_gk208_pushbuf_t *pushbuf,
    const reist_nvidia_gk208_surface_t *surface,
    const reist_nvidia_gk208_rect_t *source,
    const reist_nvidia_gk208_rect_t *destination);
int reist_nvidia_gk208_validate_pushbuf(
    const reist_nvidia_gk208_pushbuf_t *pushbuf);
int reist_nvidia_gk208_prepare_submission(
    reist_nvidia_gk208_submission_t *submission,
    const reist_nvidia_gk208_pushbuf_t *commands,
    uint64_t pushbuf_gpu_address, uint64_t fence_gpu_address,
    uint32_t fence_sequence);
int reist_nvidia_gk208_validate_submission(
    const reist_nvidia_gk208_submission_t *submission,
    uint64_t pushbuf_gpu_address, uint64_t fence_gpu_address,
    uint32_t fence_sequence);
int reist_nvidia_gk208_prepare_dma_staging(
    reist_nvidia_gk208_dma_staging_t *staging,
    const reist_nvidia_gk208_submission_t *submission,
    uint32_t fence_sequence);
int reist_nvidia_gk208_validate_dma_staging(
    const reist_nvidia_gk208_dma_staging_t *staging,
    const reist_nvidia_gk208_submission_t *submission,
    uint32_t fence_sequence);
int reist_nvidia_gk208_prepare_channel_image(
    reist_nvidia_gk208_channel_image_t *image);
int reist_nvidia_gk208_validate_channel_image(
    const reist_nvidia_gk208_channel_image_t *image);
int reist_nvidia_gk208_prepare_vm_plan(
    reist_nvidia_gk208_vm_plan_t *plan, uint32_t fb_page_shift);
int reist_nvidia_gk208_validate_vm_plan(
    const reist_nvidia_gk208_vm_plan_t *plan);
int reist_nvidia_gk208_gr_firmware_manifest(
    reist_nvidia_gk208_gr_firmware_manifest_t *manifest);
int reist_nvidia_gk208_gr_firmware_word(
    uint32_t component, uint32_t section, uint32_t index,
    uint32_t *word_out);
int reist_nvidia_gk208_gr_plan_manifest(
    reist_nvidia_gk208_gr_plan_manifest_t *manifest);
int reist_nvidia_gk208_gr_mmio_tuple(
    uint32_t pack, uint32_t index, reist_nvidia_gk208_gr_tuple_t *tuple);
int reist_nvidia_gk208_gr_context_tuple(
    uint32_t pack, uint32_t index, reist_nvidia_gk208_gr_tuple_t *tuple);
int reist_nvidia_gk208_gr_validate_topology(
    const reist_nvidia_gk208_gr_topology_t *topology);
int reist_nvidia_gk208_gr_compile_context_plan(
    reist_nvidia_gk208_gr_context_plan_t *plan);
int reist_nvidia_gk208_gr_validate_context_plan(
    const reist_nvidia_gk208_gr_context_plan_t *plan);
int reist_nvidia_gk208_gr_compile_execution_image(
    reist_nvidia_gk208_gr_execution_image_t *image,
    const reist_nvidia_gk208_gr_topology_t *topology);
int reist_nvidia_gk208_gr_validate_execution_image(
    const reist_nvidia_gk208_gr_execution_image_t *image,
    const reist_nvidia_gk208_gr_topology_t *topology);
uint32_t reist_nvidia_gk208_gr_execution_used_bytes(
    const reist_nvidia_gk208_gr_execution_image_t *image);
int reist_nvidia_gk208_gr_compile_context_memory_plan(
    reist_nvidia_gk208_gr_context_memory_plan_t *plan,
    const reist_nvidia_gk208_gr_topology_t *topology,
    uint32_t context_size);
int reist_nvidia_gk208_gr_validate_context_memory_plan(
    const reist_nvidia_gk208_gr_context_memory_plan_t *plan,
    const reist_nvidia_gk208_gr_topology_t *topology,
    uint32_t context_size);
int reist_nvidia_gk208_command_self_test(void);
int reist_nvidia_gk208_submission_self_test(void);
int reist_nvidia_gk208_dma_staging_self_test(void);
int reist_nvidia_gk208_channel_image_self_test(void);
int reist_nvidia_gk208_vm_plan_self_test(void);
int reist_nvidia_gk208_gr_firmware_self_test(void);
int reist_nvidia_gk208_gr_plan_self_test(void);
int reist_nvidia_gk208_gr_execution_self_test(void);
int reist_nvidia_gk208_gr_context_memory_self_test(void);

#endif
