#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "userspace/video/include/reist/nvidia_gk208_2d.h"

static reist_nvidia_gk208_surface_t surface(void) {
    reist_nvidia_gk208_surface_t value = {
        .gpu_address = 0x0000000010000000ULL,
        .width = 1024U,
        .height = 768U,
        .pitch = 4096U,
    };
    return value;
}

static void test_fill_and_copy_are_bounded_and_validated(void) {
    reist_nvidia_gk208_pushbuf_t pushbuf;
    reist_nvidia_gk208_surface_t target = surface();
    reist_nvidia_gk208_rect_t destination = {10U, 20U, 30U, 40U};
    assert(reist_nvidia_gk208_encode_fill(
        &pushbuf, &target, &destination, 0x00112233U) == 0);
    assert(pushbuf.word_count != 0U);
    assert(pushbuf.word_count <= REIST_NVIDIA_GK208_PUSHBUF_WORD_CAPACITY);
    assert(reist_nvidia_gk208_validate_pushbuf(&pushbuf) == 0);

    reist_nvidia_gk208_rect_t source = {1U, 2U, 30U, 40U};
    assert(reist_nvidia_gk208_encode_copy(
        &pushbuf, &target, &source, &destination) == 0);
    assert(pushbuf.word_count != 0U);
    assert(pushbuf.word_count <= REIST_NVIDIA_GK208_PUSHBUF_WORD_CAPACITY);
    assert(reist_nvidia_gk208_validate_pushbuf(&pushbuf) == 0);
}

static void test_invalid_ranges_fail_closed(void) {
    reist_nvidia_gk208_pushbuf_t pushbuf;
    reist_nvidia_gk208_surface_t target = surface();
    reist_nvidia_gk208_rect_t rect = {1000U, 760U, 25U, 9U};
    assert(reist_nvidia_gk208_encode_fill(
        &pushbuf, &target, &rect, 0U) == -34);
    rect = (reist_nvidia_gk208_rect_t){0U, 0U, 1U, 1U};
    target.gpu_address = 0x0000010000000000ULL;
    assert(reist_nvidia_gk208_encode_fill(
        &pushbuf, &target, &rect, 0U) == -34);
    target = surface();
    target.gpu_address++;
    assert(reist_nvidia_gk208_encode_fill(
        &pushbuf, &target, &rect, 0U) == -34);
    target = surface();
    target.pitch = target.width * 4U - 4U;
    assert(reist_nvidia_gk208_encode_fill(
        &pushbuf, &target, &rect, 0U) == -34);
}

static void test_stream_tampering_is_rejected(void) {
    reist_nvidia_gk208_pushbuf_t pushbuf;
    reist_nvidia_gk208_surface_t target = surface();
    reist_nvidia_gk208_rect_t rect = {0U, 0U, 8U, 8U};
    assert(reist_nvidia_gk208_encode_fill(
        &pushbuf, &target, &rect, 0x00ABCDEFU) == 0);
    pushbuf.words[0] ^= 1U << 13U;
    assert(reist_nvidia_gk208_validate_pushbuf(&pushbuf) == -84);
    assert(reist_nvidia_gk208_encode_fill(
        &pushbuf, &target, &rect, 0x00ABCDEFU) == 0);
    pushbuf.words[0] = 0xFFFFFFFFU;
    assert(reist_nvidia_gk208_validate_pushbuf(&pushbuf) == -84);
    pushbuf.word_count = REIST_NVIDIA_GK208_PUSHBUF_WORD_CAPACITY + 1U;
    assert(reist_nvidia_gk208_validate_pushbuf(&pushbuf) == -84);
}

static void test_submission_envelope_is_sealed(void) {
    reist_nvidia_gk208_pushbuf_t pushbuf;
    reist_nvidia_gk208_submission_t submission;
    reist_nvidia_gk208_surface_t target = surface();
    reist_nvidia_gk208_rect_t source = {1U, 2U, 8U, 8U};
    reist_nvidia_gk208_rect_t destination = {10U, 20U, 8U, 8U};
    const uint64_t push_address = 0x0000000020000000ULL;
    const uint64_t fence_address = 0x0000000020001000ULL;
    assert(reist_nvidia_gk208_encode_copy(
        &pushbuf, &target, &source, &destination) == 0);
    assert(reist_nvidia_gk208_prepare_submission(
        &submission, &pushbuf, push_address, fence_address, 7U) == 0);
    assert(submission.word_count == pushbuf.word_count + 7U);
    assert(submission.gpfifo_entry[0] == (uint32_t)push_address);
    assert(submission.gpfifo_entry[1] ==
        ((uint32_t)(push_address >> 32U) |
         (submission.word_count << 10U)));
    assert(reist_nvidia_gk208_validate_submission(
        &submission, push_address, fence_address, 7U) == 0);

    reist_nvidia_gk208_submission_t mutated = submission;
    mutated.words[0] ^= 1U << 13U;
    assert(reist_nvidia_gk208_validate_submission(
        &mutated, push_address, fence_address, 7U) == -84);
    mutated = submission;
    mutated.words[mutated.word_count - 1U] ^= 1U << 20U;
    assert(reist_nvidia_gk208_validate_submission(
        &mutated, push_address, fence_address, 7U) == -84);
    mutated = submission;
    mutated.gpfifo_entry[0] |= 1U;
    assert(reist_nvidia_gk208_validate_submission(
        &mutated, push_address, fence_address, 7U) == -84);
    for (uint32_t flag = 8U; flag <= 9U; ++flag) {
        mutated = submission;
        mutated.gpfifo_entry[1] |= 1U << flag;
        assert(reist_nvidia_gk208_validate_submission(
            &mutated, push_address, fence_address, 7U) == -84);
    }
    mutated = submission;
    mutated.gpfifo_entry[1] |= 1U << 31U;
    assert(reist_nvidia_gk208_validate_submission(
        &mutated, push_address, fence_address, 7U) == -84);
    mutated = submission;
    mutated.gpfifo_entry[1] ^= 1U << 10U;
    assert(reist_nvidia_gk208_validate_submission(
        &mutated, push_address, fence_address, 7U) == -84);
    mutated = submission;
    mutated.words[REIST_NVIDIA_GK208_SUBMISSION_WORD_CAPACITY - 1U] = 1U;
    assert(reist_nvidia_gk208_validate_submission(
        &mutated, push_address, fence_address, 7U) == -84);
    assert(reist_nvidia_gk208_validate_submission(
        &submission, push_address + 4U, fence_address, 7U) == -84);
    assert(reist_nvidia_gk208_validate_submission(
        &submission, push_address, fence_address, 8U) == -84);
}

static void test_submission_ranges_fail_closed(void) {
    reist_nvidia_gk208_pushbuf_t pushbuf;
    reist_nvidia_gk208_submission_t submission;
    reist_nvidia_gk208_surface_t target = surface();
    reist_nvidia_gk208_rect_t rect = {0U, 0U, 8U, 8U};
    assert(reist_nvidia_gk208_encode_fill(
        &pushbuf, &target, &rect, 0U) == 0);
    assert(reist_nvidia_gk208_prepare_submission(
        &submission, &pushbuf, 0U, 0x20001000ULL, 1U) == -84);
    assert(reist_nvidia_gk208_prepare_submission(
        &submission, &pushbuf, 0x20000002ULL, 0x20001000ULL, 1U) == -84);
    assert(reist_nvidia_gk208_prepare_submission(
        &submission, &pushbuf, 0x20000000ULL, 0x20001002ULL, 1U) == -84);
    assert(reist_nvidia_gk208_prepare_submission(
        &submission, &pushbuf, 0x20000000ULL,
        0x0000010000000000ULL, 1U) == -84);
    assert(reist_nvidia_gk208_prepare_submission(
        &submission, &pushbuf, 0x20000000ULL, 0x20001000ULL, 0U) == -84);
}

static void test_dma_staging_layout_is_fixed_and_nonoverlapping(void) {
    reist_nvidia_gk208_pushbuf_t pushbuf;
    reist_nvidia_gk208_submission_t submission;
    reist_nvidia_gk208_dma_staging_t staging;
    reist_nvidia_gk208_surface_t target = surface();
    reist_nvidia_gk208_rect_t rect = {0U, 0U, 8U, 8U};
    assert(reist_nvidia_gk208_encode_fill(
        &pushbuf, &target, &rect, 0x00112233U) == 0);
    assert(reist_nvidia_gk208_prepare_submission(
        &submission, &pushbuf, REIST_NVIDIA_GK208_PUSHBUF_GPU_ADDRESS,
        REIST_NVIDIA_GK208_FENCE_GPU_ADDRESS, 9U) == 0);
    assert(reist_nvidia_gk208_prepare_dma_staging(
        &staging, &submission, 9U) == 0);
    assert(staging.gpfifo_offset == REIST_NVIDIA_GK208_DMA_GPFIFO_OFFSET);
    assert(staging.pushbuf_offset == REIST_NVIDIA_GK208_DMA_PUSHBUF_OFFSET);
    assert(staging.fence_offset == REIST_NVIDIA_GK208_DMA_FENCE_OFFSET);
    assert(staging.gpfifo_bytes == sizeof(submission.gpfifo_entry));
    assert(staging.pushbuf_bytes == sizeof(submission.words));
    assert(staging.fence_bytes == sizeof(uint32_t));
    assert(reist_nvidia_gk208_validate_dma_staging(
        &staging, &submission, 9U) == 0);

    reist_nvidia_gk208_dma_staging_t mutated = staging;
    mutated.gpfifo_offset = REIST_NVIDIA_GK208_DMA_DESCRIPTOR_BYTES - 4U;
    assert(reist_nvidia_gk208_validate_dma_staging(
        &mutated, &submission, 9U) == -84);
    mutated = staging;
    mutated.pushbuf_offset = staging.gpfifo_offset;
    assert(reist_nvidia_gk208_validate_dma_staging(
        &mutated, &submission, 9U) == -84);
    mutated = staging;
    mutated.fence_offset = REIST_NVIDIA_GK208_DMA_POOL_BYTES;
    assert(reist_nvidia_gk208_validate_dma_staging(
        &mutated, &submission, 9U) == -84);
    mutated = staging;
    mutated.reserved = 1U;
    assert(reist_nvidia_gk208_validate_dma_staging(
        &mutated, &submission, 9U) == -84);
}

static void test_channel_image_is_exact_and_unrelocated(void) {
    reist_nvidia_gk208_channel_image_t image;
    assert(reist_nvidia_gk208_prepare_channel_image(&image) == 0);
    assert(reist_nvidia_gk208_validate_channel_image(&image) == 0);
    assert(image.ramfc_pool_offset == REIST_NVIDIA_GK208_DMA_RAMFC_OFFSET);
    assert(image.userd_pool_offset == REIST_NVIDIA_GK208_DMA_USERD_OFFSET);
    assert(image.runlist_pool_offset ==
           REIST_NVIDIA_GK208_DMA_RUNLIST_OFFSET);
    assert(image.channel_id == REIST_NVIDIA_GK208_CHANNEL_ID);
    assert(image.gpfifo_bytes == REIST_NVIDIA_GK208_GPFIFO_BYTES);
    assert(image.ramfc[0x08U / sizeof(uint32_t)] == 0U);
    assert(image.ramfc[0x0CU / sizeof(uint32_t)] == 0U);
    assert(image.ramfc[0x10U / sizeof(uint32_t)] == 0x0000FACEU);
    assert(image.ramfc[0x48U / sizeof(uint32_t)] ==
           (uint32_t)REIST_NVIDIA_GK208_GPFIFO_GPU_ADDRESS);
    assert(image.ramfc[0x4CU / sizeof(uint32_t)] == 0x00090000U);
    assert(image.ramfc[0x94U / sizeof(uint32_t)] == 0x30000001U);
    assert(image.ramfc[0xE4U / sizeof(uint32_t)] == 0U);
    assert(image.ramfc[0xE8U / sizeof(uint32_t)] ==
           REIST_NVIDIA_GK208_CHANNEL_ID);
    assert(image.runlist[0] == REIST_NVIDIA_GK208_CHANNEL_ID);
    assert(image.runlist[1] == 0U);
    assert(image.userd_relocation.destination_pool_offset ==
           REIST_NVIDIA_GK208_DMA_RAMFC_OFFSET + 0x08U);
    assert(image.userd_relocation.source_pool_offset ==
           REIST_NVIDIA_GK208_DMA_USERD_OFFSET);
    assert(image.userd_relocation.width == 8U);

    reist_nvidia_gk208_channel_image_t mutated = image;
    mutated.ramfc[0x14U / sizeof(uint32_t)] = 1U;
    assert(reist_nvidia_gk208_validate_channel_image(&mutated) == -84);
    mutated = image;
    mutated.userd[0x40U / sizeof(uint32_t)] = 1U;
    assert(reist_nvidia_gk208_validate_channel_image(&mutated) == -84);
    mutated = image;
    mutated.runlist[1] = 1U;
    assert(reist_nvidia_gk208_validate_channel_image(&mutated) == -84);
    mutated = image;
    mutated.userd_relocation.destination_pool_offset += 4U;
    assert(reist_nvidia_gk208_validate_channel_image(&mutated) == -84);
    mutated = image;
    mutated.ramfc_pool_offset = image.userd_pool_offset;
    assert(reist_nvidia_gk208_validate_channel_image(&mutated) == -84);
}

static void test_gpu_vm_plans_are_exact_and_unrelocated(void) {
    reist_nvidia_gk208_vm_plan_t plan;
    assert(REIST_NVIDIA_GK208_DEFAULT_FB_PAGE_SHIFT == 17U);
    assert(REIST_NVIDIA_GK208_SEAL_RELOCATION_COUNT == 6U);
    assert(reist_nvidia_gk208_prepare_vm_plan(
        &plan, REIST_NVIDIA_GK208_FB_PAGE_SHIFT_64K) == 0);
    assert(plan.gpu_page_shift == 12U);
    assert(plan.pgd_bits == 14U && plan.pgt_bits == 14U);
    assert(plan.pgd_bytes == 0x00020000U);
    assert(plan.pgt_bytes == 0x00020000U);
    assert(plan.vm_limit == REIST_NVIDIA_GK208_VM_LIMIT);
    assert(plan.relocation_count == REIST_NVIDIA_GK208_VM_RELOCATION_COUNT);
    assert(plan.relocations[0].destination_pool_offset ==
        REIST_NVIDIA_GK208_DMA_RAMFC_OFFSET +
            REIST_NVIDIA_GK208_RAMFC_PGD_OFFSET);
    assert(plan.relocations[0].source_pool_offset ==
        REIST_NVIDIA_GK208_DMA_PGD_OFFSET);
    assert(plan.relocations[0].shift_right == 0U);
    assert(plan.relocations[0].fixed_bits == 3ULL);
    assert(plan.relocations[1].destination_pool_offset ==
        REIST_NVIDIA_GK208_DMA_PGD_OFFSET + 8U * 8U);
    assert(plan.relocations[1].source_pool_offset ==
        REIST_NVIDIA_GK208_DMA_PGT_OFFSET);
    assert(plan.relocations[1].shift_right == 8U);
    assert(plan.relocations[1].fixed_bits == 3ULL);
    assert(plan.relocations[2].source_pool_offset ==
        REIST_NVIDIA_GK208_DMA_PUSHBUF_OFFSET);
    assert(plan.relocations[2].fixed_bits == 0x0000000600000005ULL);
    assert(plan.relocations[3].source_pool_offset ==
        REIST_NVIDIA_GK208_DMA_FENCE_OFFSET);
    assert(plan.relocations[3].fixed_bits == 0x0000000600000001ULL);
    assert(plan.relocations[4].source_pool_offset ==
        REIST_NVIDIA_GK208_DMA_GPFIFO_OFFSET);
    assert(plan.relocations[4].fixed_bits == 0x0000000600000005ULL);

    assert(reist_nvidia_gk208_prepare_vm_plan(
        &plan, REIST_NVIDIA_GK208_FB_PAGE_SHIFT_128K) == 0);
    assert(plan.pgd_bits == 13U && plan.pgt_bits == 15U);
    assert(plan.pgd_bytes == 0x00010000U);
    assert(plan.pgt_bytes == 0x00040000U);
    assert(plan.relocations[1].destination_pool_offset ==
        REIST_NVIDIA_GK208_DMA_PGD_OFFSET + 4U * 8U);
    reist_nvidia_gk208_vm_plan_t mutated = plan;
    mutated.relocations[3].fixed_bits |= 4ULL;
    assert(reist_nvidia_gk208_validate_vm_plan(&mutated) == -84);
    mutated = plan;
    mutated.relocations[4].destination_pool_offset += 8U;
    assert(reist_nvidia_gk208_validate_vm_plan(&mutated) == -84);
    mutated = plan;
    ++mutated.pgt_bytes;
    assert(reist_nvidia_gk208_validate_vm_plan(&mutated) == -84);
    assert(reist_nvidia_gk208_prepare_vm_plan(&plan, 18U) == -22);
}

static void test_gr_firmware_manifest_is_exact_and_read_only(void) {
    reist_nvidia_gk208_gr_firmware_manifest_t manifest;
    assert(sizeof(manifest) == 64U);
    assert(reist_nvidia_gk208_gr_firmware_manifest(&manifest) == 0);
    assert(manifest.version ==
           REIST_NVIDIA_GK208_GR_FIRMWARE_MANIFEST_VERSION);
    assert(manifest.struct_size == sizeof(manifest));
    assert(manifest.fecs_data_words == 193U);
    assert(manifest.fecs_code_words == 640U);
    assert(manifest.gpccs_data_words == 27U);
    assert(manifest.gpccs_code_words == 384U);
    assert(manifest.fecs_data_crc32 == 0x599287F1U);
    assert(manifest.fecs_code_crc32 == 0x761F1915U);
    assert(manifest.gpccs_data_crc32 == 0xF7976F94U);
    assert(manifest.gpccs_code_crc32 == 0xF70A347FU);
    assert(manifest.total_words == 1244U);
    assert(REIST_NVIDIA_GK208_DMA_FECS_DATA_OFFSET == 0x00070000U);
    assert(REIST_NVIDIA_GK208_DMA_FECS_CODE_OFFSET == 0x00070400U);
    assert(REIST_NVIDIA_GK208_DMA_GPCCS_DATA_OFFSET == 0x00071000U);
    assert(REIST_NVIDIA_GK208_DMA_GPCCS_CODE_OFFSET == 0x00071400U);
    assert(REIST_NVIDIA_GK208_DMA_FECS_DATA_OFFSET +
        manifest.fecs_data_words * sizeof(uint32_t) <=
        REIST_NVIDIA_GK208_DMA_FECS_CODE_OFFSET);
    assert(REIST_NVIDIA_GK208_DMA_FECS_CODE_OFFSET +
        manifest.fecs_code_words * sizeof(uint32_t) <=
        REIST_NVIDIA_GK208_DMA_GPCCS_DATA_OFFSET);
    assert(REIST_NVIDIA_GK208_DMA_GPCCS_DATA_OFFSET +
        manifest.gpccs_data_words * sizeof(uint32_t) <=
        REIST_NVIDIA_GK208_DMA_GPCCS_CODE_OFFSET);
    assert(REIST_NVIDIA_GK208_DMA_GPCCS_CODE_OFFSET +
        manifest.gpccs_code_words * sizeof(uint32_t) <=
        REIST_NVIDIA_GK208_DMA_POOL_BYTES);
    for (uint32_t index = 0U;
         index < sizeof(manifest.reserved) / sizeof(manifest.reserved[0]);
         ++index)
        assert(manifest.reserved[index] == 0U);

    uint32_t word = 0U;
    assert(reist_nvidia_gk208_gr_firmware_word(
        REIST_NVIDIA_GK208_GR_COMPONENT_FECS,
        REIST_NVIDIA_GK208_GR_SECTION_DATA, 0U, &word) == 0);
    assert(word == 0x00000300U);
    assert(reist_nvidia_gk208_gr_firmware_word(
        REIST_NVIDIA_GK208_GR_COMPONENT_FECS,
        REIST_NVIDIA_GK208_GR_SECTION_CODE, 0U, &word) == 0);
    assert(word == 0x030E0EF5U);
    assert(reist_nvidia_gk208_gr_firmware_word(
        REIST_NVIDIA_GK208_GR_COMPONENT_GPCCS,
        REIST_NVIDIA_GK208_GR_SECTION_DATA, 0U, &word) == 0);
    assert(word == 0x0000006CU);
    assert(reist_nvidia_gk208_gr_firmware_word(
        REIST_NVIDIA_GK208_GR_COMPONENT_GPCCS,
        REIST_NVIDIA_GK208_GR_SECTION_CODE, 0U, &word) == 0);
    assert(word == 0x03140EF5U);
    assert(reist_nvidia_gk208_gr_firmware_word(
        0U, REIST_NVIDIA_GK208_GR_SECTION_DATA, 0U, &word) == -34);
    assert(reist_nvidia_gk208_gr_firmware_word(
        REIST_NVIDIA_GK208_GR_COMPONENT_FECS, 0U, 0U, &word) == -34);
    assert(reist_nvidia_gk208_gr_firmware_word(
        REIST_NVIDIA_GK208_GR_COMPONENT_FECS,
        REIST_NVIDIA_GK208_GR_SECTION_DATA, 193U, &word) == -34);
    assert(reist_nvidia_gk208_gr_firmware_word(
        REIST_NVIDIA_GK208_GR_COMPONENT_FECS,
        REIST_NVIDIA_GK208_GR_SECTION_DATA, 0U, NULL) == -22);
    assert(reist_nvidia_gk208_gr_firmware_manifest(NULL) == -22);
    assert(reist_nvidia_gk208_gr_firmware_self_test() == 0);
}

static void test_gr_plan_is_pinned_bounded_and_hardware_inactive(void) {
    reist_nvidia_gk208_gr_plan_manifest_t manifest;
    assert(sizeof(manifest) == 64U);
    assert(reist_nvidia_gk208_gr_plan_manifest(&manifest) == 0);
    assert(manifest.version == REIST_NVIDIA_GK208_GR_PLAN_VERSION);
    assert(manifest.struct_size == sizeof(manifest));
    assert(manifest.mmio_pack_count == 30U);
    assert(manifest.mmio_tuple_count == 115U);
    assert(manifest.mmio_crc32 == 0xDB583025U);
    assert(manifest.context_pack_count == 5U);
    assert(manifest.context_tuple_count == 199U);
    assert(manifest.context_crc32 == 0xB765ADF0U);
    assert(manifest.hub_command_offset == 0x0040910CU);
    assert(manifest.hub_command_value == 0U);
    assert(manifest.hub_start_offset == 0x00409100U);
    assert(manifest.hub_start_value == 2U);
    assert(manifest.ready_offset == 0x00409800U);
    assert(manifest.ready_mask == 0x80000000U);
    assert(manifest.context_size_offset == 0x00409804U);
    assert(manifest.ready_deadline_ms == 2000U);

    reist_nvidia_gk208_gr_tuple_t tuple;
    assert(reist_nvidia_gk208_gr_mmio_tuple(0U, 0U, &tuple) == 0);
    assert(tuple.address == 0x00400080U && tuple.count == 1U);
    assert(reist_nvidia_gk208_gr_mmio_tuple(30U, 0U, &tuple) == -34);
    assert(reist_nvidia_gk208_gr_context_tuple(0U, 0U, &tuple) == 0);
    assert(tuple.address == 0x00400204U);
    assert(reist_nvidia_gk208_gr_context_tuple(5U, 0U, &tuple) == -34);
    assert(reist_nvidia_gk208_gr_plan_manifest(NULL) == -22);

    reist_nvidia_gk208_gr_topology_t topology = {
        .version = REIST_NVIDIA_GK208_GR_PLAN_VERSION,
        .struct_size = sizeof(topology),
        .gpc_count = 1U,
        .rop_count = 2U,
        .tpc_total = 2U,
        .tpc_max = 2U,
        .tpc_count = {2U},
        .ppc_tpc_mask = {3U},
    };
    assert(reist_nvidia_gk208_gr_validate_topology(&topology) == 0);
    topology.tpc_total = 1U;
    assert(reist_nvidia_gk208_gr_validate_topology(&topology) == -84);
    topology.tpc_total = 2U;
    topology.ppc_tpc_mask[0] = 5U;
    assert(reist_nvidia_gk208_gr_validate_topology(&topology) == -84);

    reist_nvidia_gk208_gr_context_plan_t plan;
    assert(reist_nvidia_gk208_gr_compile_context_plan(&plan) == 0);
    assert(plan.word_count > 0U);
    assert(plan.word_count <=
           REIST_NVIDIA_GK208_GR_CONTEXT_TRANSFER_CAPACITY);
    assert(reist_nvidia_gk208_gr_validate_context_plan(&plan) == 0);
    ++plan.words[0];
    assert(reist_nvidia_gk208_gr_validate_context_plan(&plan) == -84);
    assert(reist_nvidia_gk208_gr_plan_self_test() == 0);
}

static void test_gr_execution_image_is_complete_sealed_and_unresolved(void) {
    static reist_nvidia_gk208_gr_execution_image_t image;
    reist_nvidia_gk208_gr_topology_t topology = {
        .version = REIST_NVIDIA_GK208_GR_PLAN_VERSION,
        .struct_size = sizeof(topology),
        .gpc_count = 1U,
        .rop_count = 2U,
        .tpc_total = 2U,
        .tpc_max = 2U,
        .tpc_count = {2U},
        .ppc_tpc_mask = {3U},
    };
    assert(sizeof(reist_nvidia_gk208_gr_execution_header_t) == 64U);
    assert(sizeof(reist_nvidia_gk208_gr_execution_op_t) == 16U);
    assert(REIST_NVIDIA_GK208_GR_VRAM_BUFFER_BYTES == 0x20000U);
    assert(REIST_NVIDIA_GK208_GR_VRAM_BUFFER_ALIGNMENT == 0x20000U);
    assert(reist_nvidia_gk208_gr_compile_execution_image(
        &image, &topology) == 0);
    assert(reist_nvidia_gk208_gr_validate_execution_image(
        &image, &topology) == 0);
    assert(image.header.version == REIST_NVIDIA_GK208_GR_EXECUTION_VERSION);
    assert(image.header.header_size == 64U);
    assert(image.header.used_bytes ==
        64U + image.header.operation_count * 16U);
    assert(image.header.operation_count > 400U);
    assert(image.header.operation_count <=
        REIST_NVIDIA_GK208_GR_EXECUTION_OP_CAPACITY);
    assert(image.header.static_mmio_operation_count > 115U);
    assert(image.header.zbc_operation_count == 237U);
    assert(image.header.context_operation_count > 9U);
    assert(image.header.vram_relocation_count == 2U);
    assert(image.header.flags ==
        (REIST_NVIDIA_GK208_GR_EXECUTION_FLAG_HARDWARE_INACTIVE |
         REIST_NVIDIA_GK208_GR_EXECUTION_FLAG_NOFW |
         REIST_NVIDIA_GK208_GR_EXECUTION_FLAG_UNRESOLVED_VRAM));
    assert(REIST_NVIDIA_GK208_DMA_GR_EXECUTION_OFFSET +
        image.header.used_bytes <= REIST_NVIDIA_GK208_DMA_POOL_BYTES);
    assert(image.operations[0].opcode == REIST_NVIDIA_GK208_GR_OP_MASK32);
    assert(image.operations[0].address == 0x0017EA44U);
    assert(image.operations[0].value == 1U);
    assert(image.operations[0].mask == 0xFU);

    uint32_t relocation_count = 0U;
    for (uint32_t index = 0U; index < image.header.operation_count; ++index) {
        if (image.operations[index].opcode !=
            REIST_NVIDIA_GK208_GR_OP_VRAM_OFFSET32)
            continue;
        ++relocation_count;
        assert(image.operations[index].mask ==
               REIST_NVIDIA_GK208_GR_VRAM_ADDRESS_SHIFT);
        if (image.operations[index].value ==
            REIST_NVIDIA_GK208_GR_VRAM_BUFFER_MMU_WRITE)
            assert(image.operations[index].address == 0x004188B4U);
        else {
            assert(image.operations[index].value ==
                   REIST_NVIDIA_GK208_GR_VRAM_BUFFER_MMU_READ);
            assert(image.operations[index].address == 0x004188B8U);
        }
    }
    assert(relocation_count == 2U);

    const uint32_t crc = image.header.operation_crc32;
    image.operations[image.header.operation_count - 1U].value ^= 1U;
    assert(reist_nvidia_gk208_gr_validate_execution_image(
        &image, &topology) == -84);
    image.operations[image.header.operation_count - 1U].value ^= 1U;
    assert(image.header.operation_crc32 == crc);
    assert(reist_nvidia_gk208_gr_validate_execution_image(
        &image, &topology) == 0);
    ++topology.rop_count;
    assert(reist_nvidia_gk208_gr_validate_execution_image(
        &image, &topology) == -84);

    topology.gpc_count = REIST_NVIDIA_GK208_MAX_GPCS;
    topology.rop_count = REIST_NVIDIA_GK208_MAX_ROPS;
    topology.tpc_total = REIST_NVIDIA_GK208_MAX_TOTAL_TPCS;
    topology.tpc_max = 1U;
    for (uint32_t gpc = 0U; gpc < REIST_NVIDIA_GK208_MAX_GPCS; ++gpc) {
        topology.tpc_count[gpc] = 1U;
        topology.ppc_tpc_mask[gpc] = 1U;
    }
    assert(reist_nvidia_gk208_gr_compile_execution_image(
        &image, &topology) == 0);
    assert(reist_nvidia_gk208_gr_validate_execution_image(
        &image, &topology) == 0);
    assert(image.header.operation_count <
        REIST_NVIDIA_GK208_GR_EXECUTION_OP_CAPACITY);
    assert(reist_nvidia_gk208_gr_execution_self_test() == 0);
}

static void test_gr_context_memory_plan_is_topology_bound_and_bounded(void) {
    reist_nvidia_gk208_gr_topology_t topology = {
        .version = REIST_NVIDIA_GK208_GR_PLAN_VERSION,
        .struct_size = sizeof(topology),
        .gpc_count = 1U,
        .rop_count = 2U,
        .tpc_total = 2U,
        .tpc_max = 2U,
        .tpc_count = {2U},
        .ppc_tpc_mask = {3U},
    };
    reist_nvidia_gk208_gr_context_memory_plan_t plan;
    assert(sizeof(plan) == 64U);
    assert(reist_nvidia_gk208_gr_compile_context_memory_plan(
        &plan, &topology, 0x2000U) == 0);
    assert(plan.pagepool_bytes == 0x8000U);
    assert(plan.bundle_bytes == 0x3000U);
    assert(plan.attrib_bytes == 0x2C8C0U);
    assert(plan.golden_cb_reserved == 0x80000U);
    assert(plan.golden_bytes == 0x82000U);
    assert(plan.total_bytes == 0xBA000U);
    assert(reist_nvidia_gk208_gr_validate_context_memory_plan(
        &plan, &topology, 0x2000U) == 0);
    reist_nvidia_gk208_gr_context_memory_plan_t mutated = plan;
    ++mutated.attrib_bytes;
    assert(reist_nvidia_gk208_gr_validate_context_memory_plan(
        &mutated, &topology, 0x2000U) == -84);
    assert(reist_nvidia_gk208_gr_compile_context_memory_plan(
        &plan, &topology, 0U) == -22);
    assert(reist_nvidia_gk208_gr_context_memory_self_test() == 0);
}

int main(void) {
    test_fill_and_copy_are_bounded_and_validated();
    test_invalid_ranges_fail_closed();
    test_stream_tampering_is_rejected();
    test_submission_envelope_is_sealed();
    test_submission_ranges_fail_closed();
    test_dma_staging_layout_is_fixed_and_nonoverlapping();
    test_channel_image_is_exact_and_unrelocated();
    test_gpu_vm_plans_are_exact_and_unrelocated();
    test_gr_firmware_manifest_is_exact_and_read_only();
    test_gr_plan_is_pinned_bounded_and_hardware_inactive();
    test_gr_execution_image_is_complete_sealed_and_unresolved();
    test_gr_context_memory_plan_is_topology_bound_and_bounded();
    assert(reist_nvidia_gk208_submission_self_test() == 0);
    assert(reist_nvidia_gk208_dma_staging_self_test() == 0);
    assert(reist_nvidia_gk208_channel_image_self_test() == 0);
    assert(reist_nvidia_gk208_vm_plan_self_test() == 0);
    assert(reist_nvidia_gk208_gr_firmware_self_test() == 0);
    assert(reist_nvidia_gk208_gr_plan_self_test() == 0);
    assert(reist_nvidia_gk208_gr_execution_self_test() == 0);
    assert(reist_nvidia_gk208_gr_context_memory_self_test() == 0);
    return 0;
}
