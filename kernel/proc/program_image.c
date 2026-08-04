#include "kernel/proc/program_image.h"

#include "kernel/init/prg.h"

#define PROGRAM_IMAGE_MAGIC 0xDEADBEEFU

int program_image_validate(const void* image, uint32_t image_size,
                           uint32_t region_size) {
    if (!image || image_size < sizeof(program_header_t) ||
        image_size > region_size) {
        return -1;
    }

    const program_header_t* header = (const program_header_t*)image;
    if (header->identifier[0] != 'M' || header->identifier[1] != 'Y' ||
        header->identifier[2] != 'P' || header->identifier[3] != 'R' ||
        header->magic_number != PROGRAM_IMAGE_MAGIC ||
        header->program_size == 0 ||
        header->program_size > region_size - sizeof(program_header_t) ||
        header->base_address == 0 ||
        header->base_address > UINT32_MAX -
                                   (sizeof(program_header_t) +
                                    header->program_size)) {
        return -1;
    }

    uint32_t program_end = (uint32_t)sizeof(program_header_t) +
                           header->program_size;
    if (header->entry_point < sizeof(program_header_t) ||
        header->entry_point >= program_end ||
        header->entry_point >= header->relocation_offset ||
        header->relocation_offset < sizeof(program_header_t) ||
        header->relocation_offset > program_end ||
        header->relocation_offset > image_size ||
        (header->relocation_offset % sizeof(uint32_t)) != 0 ||
        header->relocation_size > image_size - header->relocation_offset ||
        (header->relocation_size % sizeof(uint32_t)) != 0) {
        return -1;
    }
    return 0;
}
