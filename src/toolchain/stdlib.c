#include <stddef.h>
#include <stdint.h>

// custom implementations
#include "stdlib.h"
#include "stdio.h"


typedef struct memory_block {
    size_t size;
    int free;
    struct memory_block *next;
} memory_block;

// Kernel starts at 0x100000 1MB address in memory
// Heap starts at 0x100000 + Kernel Size
// TODO: Get the kernel size from the linker script and use the memory addresses dynamically from the bootloader
#define HEAP_START  (void*)(0x100000 + (1024 * 1024))  // 2MB
#define HEAP_END    (void*)0x500000  // 5MB

#define BLOCK_SIZE sizeof(memory_block)

memory_block *freeList = (memory_block*)HEAP_START;

void initialize_memory_system() {
    // Initialize memory pool on first malloc call
    freeList->size = (size_t)HEAP_END - (size_t)HEAP_START - BLOCK_SIZE;
    freeList->free = 1;
    freeList->next = NULL;
}

void *malloc(size_t size) {
    memory_block *current = freeList, *prev = NULL;
    void *result = NULL;

    // Align size to word boundary
    size = (size + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1);

    while (current) {
        // Check if current block is free and large enough
        if (current->free && current->size >= size) {
            // Check if we can split the block
            if (current->size > size + BLOCK_SIZE + sizeof(size_t)) {
                memory_block *newBlock = (memory_block*)((size_t)current + BLOCK_SIZE + size);
                newBlock->size = current->size - size - BLOCK_SIZE;
                newBlock->free = 1;
                newBlock->next = current->next;
                current->size = size;
                current->next = newBlock;
            }
            current->free = 0;
            result = (void*)((size_t)current + BLOCK_SIZE);
            break;
        }
        prev = current;
        current = current->next;
    }

    return result;
}

void free(void *ptr) {
    if (!ptr) {
        return;
    }

    memory_block *block = (memory_block*)((size_t)ptr - BLOCK_SIZE);
    block->free = 1;

    // Coalescing free blocks
    memory_block *current = freeList;
    while (current) {
        if (current->free && current->next && current->next->free) {
            current->size += current->next->size + BLOCK_SIZE;
            current->next = current->next->next;
        }
        current = current->next;
    }
}

void secure_free(void *ptr, size_t size) {
    if (ptr != NULL) {
        memset(ptr, 0, size);
        free(ptr);
    }
}

int memcmp(const void* s1, const void* s2, size_t n) {
    if (s1 == NULL || s2 == NULL) {
        return -1; // Error handling for NULL pointers
    }

    const unsigned char* p1 = (const unsigned char*)s1;
    const unsigned char* p2 = (const unsigned char*)s2;

    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }

    return 0;
}

void* memset(void* ptr, int value, unsigned int num) {
    if (ptr == NULL) {
        return NULL; // Error handling for NULL pointer
    }

    unsigned char* p = (unsigned char*)ptr;
    while (num--) {
        *p++ = (unsigned char)value;
    }
    return ptr;
}

/// <summary>
/// Copies the values of num bytes from the location pointed by source directly to the memory block pointed by destination.
/// </summary>
void* memcpy(void* dest, const void* src, unsigned int n) {
    if (dest == NULL || src == NULL) {
        return NULL; // Error handling for NULL pointers
    }

    char* d = (char*)dest;
    const char* s = (const char*)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

// test methods

void TestResetAfterFree() {
    void* firstPtr = malloc(1);
    //printf("First allocation: %p\n", firstPtr);

    free(firstPtr); // This should store the initial alignment padding

    void* secondPtr = malloc(1);
    //printf("Second allocation: %p\n", secondPtr);

    if (firstPtr == secondPtr) {
        //printf("TestResetAfterFree: Passed\n");
    } else {
        printf("TestResetAfterFree: Failed. Expected: %p, Got: %p\n", firstPtr, secondPtr);
    }
}

void TestMultipleFrees() {
    free(NULL);
    free(NULL);
    void* ptr = malloc(1);

    if (ptr != NULL) {
        //printf("TestMultipleFrees: Passed\n");
    } else {
        printf("TestMultipleFrees: Failed\n");
    }
}
void TestSetMemory() {
    char* buffer = (char*)malloc(10);
    memset(buffer, 'A', 10);

    int pass = 1;
    for (int i = 0; i < 10; i++) {
        if (buffer[i] != 'A') {
            pass = 0;
            break;
        }
    }

    if(pass) {
        //printf("TestSetMemory: Passed\n");
    } else {
        printf("TestSetMemory: Failed\n");
    }
}
void TestSetZero() {
    char* buffer = (char*)malloc(10);
    memset(buffer, 0, 10);

    int pass = 1;
    for (int i = 0; i < 10; i++) {
        if (buffer[i] != 0) {
            pass = 0;
            break;
        }
    }

    if(pass) {
        //printf("TestSetZero: Passed\n");
    } else {
        printf("TestSetZero: Failed\n");
    }
}
void TestNullPointerMemset() {
    if (memset(NULL, 0, 10) == NULL) {
        //printf("TestNullPointerMemset: Passed\n");
    } else {
        printf("TestNullPointerMemset: Failed\n");
    }
}
void TestCopyNonOverlapping() {
    char src[10] = "123456789";
    char dest[10];
    memcpy(dest, src, 10);

    int pass = 1;
    for (int i = 0; i < 10; i++) {
        if (dest[i] != src[i]) {
            pass = 0;
            break;
        }
    }

    if(pass) {
        //printf("TestCopyNonOverlapping: Passed\n");
    } else {
        printf("TestCopyNonOverlapping: Failed\n");
    }
}
void TestCopyOverlapping() {
    char buffer[20] = "123456789";
    memcpy(buffer + 4, buffer, 10);

    int pass = 1;
    for (int i = 0; i < 10; i++) {
        if (buffer[i + 4] != buffer[i]) {
            pass = 0;
            break;
        }
    }

    if(pass) {
        //printf("TestCopyOverlapping: Passed\n");
    } else {
        printf("TestCopyOverlapping: Failed\n");
    }
}
void TestNullPointerSrc() {
    char dest[10];
    if (memcpy(dest, NULL, 10) == NULL) {
        //printf("TestNullPointerSrc: Passed\n");
    } else {
        printf("TestNullPointerSrc: Failed\n");
    }
}
void TestNullPointerDest() {
    char src[10] = "123456789";
    if (memcpy(NULL, src, 10) == NULL) {
        //printf("TestNullPointerDest: Passed\n");
    } else {
        printf("TestNullPointerDest: Failed\n");
    }
}

int test_memory() {
    printf("Testing Memory...");
    TestResetAfterFree();
    TestMultipleFrees();
    TestSetMemory();
    TestSetZero();
    TestNullPointerMemset();
    TestCopyNonOverlapping();
    TestCopyOverlapping();
    TestNullPointerSrc();
    TestNullPointerDest();
    printf("done\n");
    return 0;
}
