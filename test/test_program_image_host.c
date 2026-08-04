#include "kernel/proc/program_image.h"

#include "kernel/init/prg.h"

#include <string.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

int main(void) {
    unsigned char image[64];
    memset(image, 0, sizeof(image));
    program_header_t* header = (program_header_t*)image;
    memcpy(header->identifier, "MYPR", 4);
    header->magic_number = 0xDEADBEEF;
    header->entry_point = sizeof(*header);
    header->program_size = 4;
    header->base_address = 0x02100000;
    header->relocation_offset = 32;
    header->relocation_size = 0;
    CHECK(program_image_validate(image, 32, 8U * 1024U * 1024U) == 0);

    header->entry_point = 32;
    CHECK(program_image_validate(image, 32, 8U * 1024U * 1024U) != 0);
    header->entry_point = sizeof(*header);
    header->relocation_offset = 28;
    CHECK(program_image_validate(image, 32, 8U * 1024U * 1024U) != 0);
    header->relocation_offset = 32;
    header->program_size = 40;
    CHECK(program_image_validate(image, 32, 8U * 1024U * 1024U) == 0);
    header->program_size = 8U * 1024U * 1024U;
    CHECK(program_image_validate(image, 32, 8U * 1024U * 1024U) != 0);
    header->program_size = 4;
    header->identifier[0] = 'X';
    CHECK(program_image_validate(image, 32, 8U * 1024U * 1024U) != 0);
    return 0;
}
