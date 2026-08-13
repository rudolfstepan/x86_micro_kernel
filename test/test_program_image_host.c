#include "kernel/proc/program_image.h"

#include "kernel/init/prg.h"

#include <stdint.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

#define VALID_IMAGE_SIZE 32U

static program_header_t* make_valid_image(unsigned char* image,
                                          uint32_t capacity) {
    memset(image, 0, capacity);
    program_header_t* header = (program_header_t*)image;
    memcpy(header->identifier, "MYPR", 4);
    header->magic_number = 0xDEADBEEFU;
    header->entry_point = sizeof(*header);
    /* Four stored payload bytes followed by eight zero-filled BSS bytes. */
    header->program_size = 12U;
    header->base_address = PROGRAM_V1_BASE;
    header->relocation_offset = VALID_IMAGE_SIZE;
    header->relocation_size = 0;
    return header;
}

int main(void) {
    unsigned char image[64];
    program_header_t* header;

    CHECK(sizeof(program_header_t) == 28U);

    /* PRG v1 is a fixed-address image.  Stored bytes may be shorter than the
     * in-memory payload because the loader zero-fills BSS. */
    header = make_valid_image(image, sizeof(image));
    CHECK(program_image_validate(image, VALID_IMAGE_SIZE,
                                 PROGRAM_V1_REGION_SIZE) == 0);

    header = make_valid_image(image, sizeof(image));
    header->base_address = PROGRAM_V1_BASE + 0x1000U;
    CHECK(program_image_validate(image, VALID_IMAGE_SIZE,
                                 PROGRAM_V1_REGION_SIZE) != 0);

    /* Even a structurally well-formed relocation table is unsupported in
     * PRG v1 and must be rejected before the image is mapped. */
    header = make_valid_image(image, sizeof(image));
    header->relocation_size = sizeof(uint32_t);
    CHECK(program_image_validate(image, VALID_IMAGE_SIZE + sizeof(uint32_t),
                                 PROGRAM_V1_REGION_SIZE) != 0);

    /* With no relocation table, relocation_offset is exactly the file end;
     * neither hidden trailing bytes nor an embedded table are permitted. */
    header = make_valid_image(image, sizeof(image));
    CHECK(program_image_validate(image, VALID_IMAGE_SIZE + sizeof(uint32_t),
                                 PROGRAM_V1_REGION_SIZE) != 0);

    header = make_valid_image(image, sizeof(image));
    header->relocation_offset = sizeof(*header) + sizeof(uint32_t);
    header->relocation_size = 2U * sizeof(uint32_t);
    CHECK(program_image_validate(image, sizeof(*header) + 3U * sizeof(uint32_t),
                                 PROGRAM_V1_REGION_SIZE) != 0);

    header = make_valid_image(image, sizeof(image));
    header->relocation_offset = VALID_IMAGE_SIZE + 1U;
    CHECK(program_image_validate(image, VALID_IMAGE_SIZE + 1U,
                                 PROGRAM_V1_REGION_SIZE) != 0);

    /* The entry point must name a stored payload byte.  Header bytes, the
     * first byte after the file, and zero-filled BSS are not executable. */
    header = make_valid_image(image, sizeof(image));
    header->entry_point = sizeof(*header) - 1U;
    CHECK(program_image_validate(image, VALID_IMAGE_SIZE,
                                 PROGRAM_V1_REGION_SIZE) != 0);

    header = make_valid_image(image, sizeof(image));
    header->entry_point = VALID_IMAGE_SIZE;
    CHECK(program_image_validate(image, VALID_IMAGE_SIZE,
                                 PROGRAM_V1_REGION_SIZE) != 0);

    header = make_valid_image(image, sizeof(image));
    header->entry_point = VALID_IMAGE_SIZE + sizeof(uint32_t);
    CHECK(program_image_validate(image, VALID_IMAGE_SIZE,
                                 PROGRAM_V1_REGION_SIZE) != 0);

    /* The stored image must fit both the declared memory image and the
     * caller-provided loader region. */
    header = make_valid_image(image, sizeof(image));
    header->program_size = sizeof(uint32_t);
    header->relocation_offset = VALID_IMAGE_SIZE + sizeof(uint32_t);
    CHECK(program_image_validate(image, VALID_IMAGE_SIZE + sizeof(uint32_t),
                                 PROGRAM_V1_REGION_SIZE) != 0);

    header = make_valid_image(image, sizeof(image));
    header->program_size = PROGRAM_V1_REGION_SIZE - sizeof(*header) + 1U;
    CHECK(program_image_validate(image, VALID_IMAGE_SIZE,
                                 PROGRAM_V1_REGION_SIZE) != 0);

    header = make_valid_image(image, sizeof(image));
    CHECK(program_image_validate(image, VALID_IMAGE_SIZE,
                                 VALID_IMAGE_SIZE - 1U) != 0);

    /* This addition would wrap while deriving header + memory payload. */
    header = make_valid_image(image, sizeof(image));
    header->program_size = UINT32_MAX - (uint32_t)sizeof(*header) + 1U;
    CHECK(program_image_validate(image, VALID_IMAGE_SIZE, UINT32_MAX) != 0);

    header = make_valid_image(image, sizeof(image));
    header->identifier[0] = 'X';
    CHECK(program_image_validate(image, VALID_IMAGE_SIZE,
                                 PROGRAM_V1_REGION_SIZE) != 0);

    header = make_valid_image(image, sizeof(image));
    header->magic_number ^= 1U;
    CHECK(program_image_validate(image, VALID_IMAGE_SIZE,
                                 PROGRAM_V1_REGION_SIZE) != 0);

    return 0;
}
