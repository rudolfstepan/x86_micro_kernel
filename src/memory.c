#include <stddef.h>
#include <stdint.h>

#include "memory.h"
#include "system.h"


//#define ALIGNMENT 4  // Change this as needed (e.g., 8 for 8-byte alignment)
#define HEAP_SIZE 4024 * 1024  // 1 MB heap size

static unsigned char heap[HEAP_SIZE];

typedef struct Block {
    size_t size;
    struct Block* next;
} Block;

static Block* freeList = (Block*)heap;

void initializeHeap() {
    freeList->size = HEAP_SIZE - sizeof(Block);
    freeList->next = NULL;
}

void* malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    Block* current = freeList;
    Block* prev = NULL;

    size += sizeof(Block); // Add size of the header

    while (current != NULL) {
        if (current->size >= size) {
            if (current->size > size + sizeof(Block)) {
                // Split the block
                Block* newBlock = (Block*)((char*)current + size);
                newBlock->size = current->size - size;
                newBlock->next = current->next;
                current->size = size;
                current->next = newBlock;
            }

            if (prev != NULL) {
                prev->next = current->next;
            } else {
                freeList = current->next;
            }

            return (char*)current + sizeof(Block);
        }

        prev = current;
        current = current->next;
    }

    return NULL; // No suitable block found
}

void free(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    Block* block = (Block*)((char*)ptr - sizeof(Block));
    block->next = freeList;
    freeList = block;
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








// #define HEAP_SIZE 1024 * 1024  // 1 MB heap size

// static unsigned char heap[HEAP_SIZE];
// static void* current_heap_ptr = heap;

// void* malloc(unsigned int size) {
//     // Cast current_heap_ptr to unsigned char* for pointer arithmetic
//     if (((unsigned char*)current_heap_ptr - heap) + size > HEAP_SIZE) {
//         return NULL; // Heap out of memory
//     }

//     void* allocated_memory = current_heap_ptr;
//     current_heap_ptr = (unsigned char*)current_heap_ptr + size;

//     // Ensure the allocated memory is aligned, for example, to 8 bytes
//     unsigned int alignment = 8;
//     unsigned int misalignment = (size_t)current_heap_ptr & (alignment - 1);
//     if (misalignment) {
//         current_heap_ptr = (unsigned char*)current_heap_ptr + (alignment - misalignment);
//     }

//     return allocated_memory;
// }


// char memoryPool[MEMORY_POOL_SIZE];
// unsigned int memoryPoolIndex = 0;
// unsigned int initialAlignmentPadding = 0; // Store the initial alignment offset after free

// void* malloc_(unsigned int size) {
//     if (size == 0) {
//         return NULL;
//     }

//     // Calculate the next aligned address only if memoryPoolIndex is not zero.
//     unsigned int alignmentPadding = 0;
//     if (memoryPoolIndex != 0) {
//         uintptr_t currentAddress = (uintptr_t)&memoryPool[memoryPoolIndex];
//         alignmentPadding = (ALIGNMENT - (currentAddress % ALIGNMENT)) % ALIGNMENT;
//     }

//     // Calculate the total size (requested size + alignment padding)
//     unsigned int totalSize = size + alignmentPadding;
//     if (memoryPoolIndex + totalSize > MEMORY_POOL_SIZE) {
//         return NULL; // Not enough memory
//     }

//     // Move the index past the alignment padding
//     memoryPoolIndex += alignmentPadding;
//     // Get the pointer to the newly allocated memory
//     void* allocatedMemory = &memoryPool[memoryPoolIndex];
//     // Move the index past the allocated memory
//     memoryPoolIndex += size;

//     return allocatedMemory;
// }

// void free() {
//     // Reset the memory pool index and the initial alignment padding
//     memoryPoolIndex = 0;
//     initialAlignmentPadding = 0; // Ensure this is set to 0 as we're at the start of the pool
// }

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
void TestAllocationWithinBounds() {
    void* ptr = malloc(HEAP_SIZE / 2);
    if (ptr != NULL) {
        printf("TestAllocationWithinBounds: Passed\n");
    } else {
        printf("TestAllocationWithinBounds: Failed\n");
    }
}
void TestAllocationExceedsBounds() {
    void* ptr = malloc(HEAP_SIZE + 1);
    if (ptr == NULL) {
        printf("TestAllocationExceedsBounds: Passed\n");
    } else {
        printf("TestAllocationExceedsBounds: Failed\n");
    }
}
// void TestAlignment() {
//     void* ptr = malloc(1);
//     if (((uintptr_t)ptr % ALIGNMENT) == 0) {
//         printf("TestAlignment: Passed\n");
//     } else {
//         printf("TestAlignment: Failed\n");
//     }
// }
// void TestSequentialAllocations() {
//     void* firstPtr = malloc(1);
//     void* secondPtr = malloc(1);

//     if (secondPtr == firstPtr + ALIGNMENT) {
//         printf("TestSequentialAllocations: Passed\n");
//     } else {
//         printf("TestSequentialAllocations: Failed\n");
//     }
// }

void TestResetAfterFree() {
    void* firstPtr = malloc(1);
    //printf("First allocation: %p\n", firstPtr);

    free(firstPtr); // This should store the initial alignment padding

    void* secondPtr = malloc(1);
    //printf("Second allocation: %p\n", secondPtr);

    if (firstPtr == secondPtr) {
        printf("TestResetAfterFree: Passed\n");
    } else {
        printf("TestResetAfterFree: Failed. Expected: %p, Got: %p\n", firstPtr, secondPtr);
    }
}

void TestMultipleFrees() {
    free(NULL);
    free(NULL);
    void* ptr = malloc(1);

    if (ptr != NULL) {
        printf("TestMultipleFrees: Passed\n");
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

    printf("TestSetMemory: %s\n", pass ? "Passed" : "Failed");
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

    printf("TestSetZero: %s\n", pass ? "Passed" : "Failed");
}
void TestNullPointerMemset() {
    if (memset(NULL, 0, 10) == NULL) {
        printf("TestNullPointerMemset: Passed\n");
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

    printf("TestCopyNonOverlapping: %s\n", pass ? "Passed" : "Failed");
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

    printf("TestCopyOverlapping: %s\n", pass ? "Passed" : "Failed");
}
void TestNullPointerSrc() {
    char dest[10];
    if (memcpy(dest, NULL, 10) == NULL) {
        printf("TestNullPointerSrc: Passed\n");
    } else {
        printf("TestNullPointerSrc: Failed\n");
    }
}
void TestNullPointerDest() {
    char src[10] = "123456789";
    if (memcpy(NULL, src, 10) == NULL) {
        printf("TestNullPointerDest: Passed\n");
    } else {
        printf("TestNullPointerDest: Failed\n");
    }
}

int test_memory() {
    // printf("Running memory tests...\n");
    // printf("Memory pool starts at: %p\n", (void*)memoryPool);
    // printf("Memory pool ends at: %p\n", (void*)(memoryPool + MEMORY_POOL_SIZE));
    TestResetAfterFree();
    TestMultipleFrees();
    TestAllocationWithinBounds();
    TestAllocationExceedsBounds();
    // TestAlignment();
    // TestSequentialAllocations();
    TestSetMemory();
    TestSetZero();
    TestNullPointerMemset();
    TestCopyNonOverlapping();
    TestCopyOverlapping();
    TestNullPointerSrc();
    TestNullPointerDest();

    return 0;
}

