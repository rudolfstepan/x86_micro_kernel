#include <stddef.h>
#include <stdint.h>

// custom implementations
#include "stdlib.h"
#include "stdio.h"


#define POOL_SIZE 1024 * 1024  // Adjust this size according to your needs

typedef struct Block {
    size_t size;          // Size of the block
    struct Block *next;   // Pointer to the next block
} Block;

#define ALIGNMENT 8  // Align blocks to 8 bytes
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~(ALIGNMENT-1))
#define BLOCK_SIZE (ALIGN(sizeof(Block)))

static char memoryPool[POOL_SIZE];  // Define the memory pool
static Block *freeList = (Block *)memoryPool; // Initialize the free list


void initializeHeap() {
    // Initialize memory pool on first malloc call

}

void *malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    size = ALIGN(size);  // Align the requested size
    Block *bestFit = NULL;
    Block **bestFitPrev = NULL;

    // Initialize the memory pool if it's the first call
    if (!freeList->size) {
        freeList->size = POOL_SIZE - BLOCK_SIZE;
        freeList->next = NULL;
    }

    Block *prev = NULL;
    Block *current = freeList;

    // Find the best-fit block
    while (current != NULL) {
        if (current->size >= size) {
            if (!bestFit || current->size < bestFit->size) {
                bestFit = current;
                bestFitPrev = prev ? &prev->next : &freeList;
            }
        }
        prev = current;
        current = current->next;
    }

    // No suitable block found
    if (!bestFit) {
        return NULL;
    }

    // Split the block if large enough
    if (bestFit->size >= size + BLOCK_SIZE + ALIGNMENT) {
        Block *newBlock = (Block *)((char *)bestFit + BLOCK_SIZE + size);
        newBlock->size = bestFit->size - size - BLOCK_SIZE;
        newBlock->next = bestFit->next;
        bestFit->size = size;
        bestFit->next = newBlock;
    } else {
        *bestFitPrev = bestFit->next; // Remove bestFit from free list
    }

    return (char *)bestFit + BLOCK_SIZE;
}

void free(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    ptr = NULL; // Clear the pointer to the freed block

    return;

    Block *blockToFree = (Block *)((char *)ptr - BLOCK_SIZE);
    Block *current = freeList;
    Block *prev = NULL;

    // Find the correct place to insert the freed block in the sorted free list
    while (current != NULL && current < blockToFree) {
        prev = current;
        current = current->next;
    }

    // Coalesce with the next block if adjacent
    if ((char *)blockToFree + blockToFree->size + BLOCK_SIZE == (char *)current) {
        blockToFree->size += current->size + BLOCK_SIZE;
        blockToFree->next = current->next;
    } else {
        blockToFree->next = current;
    }

    // Coalesce with the previous block if adjacent
    if (prev && (char *)prev + prev->size + BLOCK_SIZE == (char *)blockToFree) {
        prev->size += blockToFree->size + BLOCK_SIZE;
        prev->next = blockToFree->next;
    } else if (prev) {
        prev->next = blockToFree;
    } else {
        freeList = blockToFree;
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
void TestAllocationWithinBounds() {
    void* ptr = malloc(POOL_SIZE / 2);
    if (ptr != NULL) {
        //printf("TestAllocationWithinBounds: Passed\n");
    } else {
        printf("TestAllocationWithinBounds: Failed\n");
    }
}
void TestAllocationExceedsBounds() {
    void* ptr = malloc(POOL_SIZE + 1);
    if (ptr == NULL) {
        //printf("TestAllocationExceedsBounds: Passed\n");
    } else {
        printf("TestAllocationExceedsBounds: Failed\n");
    }
}
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
    // printf("Memory pool starts at: %p\n", (void*)memoryPool);
    // printf("Memory pool ends at: %p\n", (void*)(memoryPool + MEMORY_POOL_SIZE));
    TestResetAfterFree();
    TestMultipleFrees();
    TestAllocationWithinBounds();
    TestAllocationExceedsBounds();
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

