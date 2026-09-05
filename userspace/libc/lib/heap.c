/* ISO C11 object allocation in caller-owned or explicitly admitted Ring-3
 * backing. Out-of-band metadata never dereferences a caller's preceding bytes.
 * Whole empty provider regions return automatically; ordinary OOM is atomic.
 * One execution context per process, no reentrant callbacks or thread claim. */
#include <reist/libc.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define HEAP_SLOTS (REIST_LIBC_OBJECT_LIMIT * 2U + REIST_LIBC_BACKING_REGIONS)
#define HEAP_END UINT32_MAX
#define HEAP_ALIGN ((uint32_t)_Alignof(max_align_t))
typedef struct { uint32_t offset, span, requested, next, check; } heap_block_t;
typedef struct {
    unsigned char *base;
    uintptr_t base_check;
    uint32_t capacity, head, check;
} heap_area_t;
static heap_block_t heap_blocks[HEAP_SLOTS];
static heap_area_t heap_areas[REIST_LIBC_BACKING_REGIONS];
static reist_libc_backing_t heap_provider;
static uintptr_t provider_check;
static uint32_t heap_initialized, heap_capacity, heap_live, heap_bytes, heap_peak;
static int heap_busy, heap_errno;
int *reist_libc_errno(void) { return &heap_errno; }
static _Noreturn void bad_heap(void) { reist_libc_fail(REIST_LIBC_FAULT_HEAP); }
static uint32_t block_check(const heap_block_t *b) {
    return ~(b->offset ^ b->span ^ b->requested ^ b->next ^ 0x52434831U);
}
static void seal(heap_block_t *b) { b->check=block_check(b); }
static void seal_area(heap_area_t *a) {
    a->base_check=~(uintptr_t)a->base;
    a->check=~(a->capacity ^ a->head ^ 0x52434131U);
}
static uintptr_t backing_check(void) {
    return ~((uintptr_t)heap_provider.acquire ^ (uintptr_t)heap_provider.release ^
        (uintptr_t)heap_provider.context ^ heap_provider.budget ^ heap_provider.quantum ^
        heap_provider.version ^ heap_provider.struct_size);
}
/* A contiguous, nonoverlapping partition of each admitted region. Every node
 * has a positive span, every walk and the aggregate visit count are bounded.
 * Fixed metadata also bounds object fragmentation independently of RAM size. */
static void validate(void) {
    if (heap_busy) bad_heap();
    if (!heap_initialized) {
        if (heap_capacity || heap_live || heap_bytes || heap_peak ||
            heap_provider.acquire || provider_check) bad_heap();
        return;
    }
    if (heap_initialized!=1U) bad_heap();
    uint32_t limit=REIST_LIBC_HEAP_LIMIT;
    if (heap_provider.acquire) {
        if (provider_check!=backing_check() || !heap_provider.release ||
            heap_provider.version!=REIST_LIBC_BACKING_VERSION ||
            heap_provider.struct_size!=sizeof(heap_provider) ||
            !heap_provider.budget || heap_provider.budget>REIST_LIBC_PROCESS_LIMIT ||
            !heap_provider.quantum || heap_provider.quantum%HEAP_ALIGN)
            bad_heap();
        limit=heap_provider.budget;
    } else if (provider_check) bad_heap();
    uint32_t capacity=0, objects=0, bytes=0, visited=0;
    for (uint32_t i=0;i<REIST_LIBC_BACKING_REGIONS;++i) {
        const heap_area_t *a=&heap_areas[i];
        if (!a->base) {
            if (a->base_check || a->capacity || a->head || a->check) bad_heap();
            continue;
        }
        if (a->base_check!=~(uintptr_t)a->base ||
            a->check!=~(a->capacity ^ a->head ^ 0x52434131U) ||
            (uintptr_t)a->base%HEAP_ALIGN ||
            !a->capacity || a->capacity%HEAP_ALIGN || a->capacity>limit-capacity ||
            (uintptr_t)a->base>UINTPTR_MAX-a->capacity) bad_heap();
        capacity+=a->capacity;
        uint32_t covered=0;
        for (uint32_t at=a->head;at!=HEAP_END;at=heap_blocks[at].next) {
            if (at>=HEAP_SLOTS || ++visited>HEAP_SLOTS) bad_heap();
            const heap_block_t *b=&heap_blocks[at];
            if (b->check!=block_check(b) || b->offset!=covered || !b->span ||
                b->span%HEAP_ALIGN || b->span>a->capacity-covered ||
                b->requested>b->span) bad_heap();
            covered+=b->span;
            if (b->requested) { ++objects; bytes+=b->requested; }
        }
        if (covered!=a->capacity) bad_heap();
    }
    if (capacity!=heap_capacity || objects!=heap_live || bytes!=heap_bytes ||
        objects>REIST_LIBC_OBJECT_LIMIT || heap_peak<bytes || heap_peak>limit)
        bad_heap();
}
static uint32_t vacant_block(void) {
    for (uint32_t i=0;i<HEAP_SLOTS;++i) if (!heap_blocks[i].span) return i;
    return HEAP_END;
}
static void add_area(uint32_t area, uint32_t node, void *storage, uint32_t capacity) {
    heap_areas[area]=(heap_area_t){storage,0,capacity,node,0};
    seal_area(&heap_areas[area]);
    heap_blocks[node]=(heap_block_t){0,capacity,0,HEAP_END,0};
    seal(&heap_blocks[node]);
    heap_capacity+=capacity;
}
int reist_libc_init(void *storage, size_t capacity) {
    validate();
    if (heap_initialized) return -EBUSY;
    if (!storage || (uintptr_t)storage%HEAP_ALIGN || capacity<HEAP_ALIGN ||
        capacity>REIST_LIBC_HEAP_LIMIT || capacity%HEAP_ALIGN ||
        (uintptr_t)storage>UINTPTR_MAX-capacity) return -EINVAL;
    heap_initialized=1U;
    add_area(0,0,storage,(uint32_t)capacity);
    return 0;
}
int reist_libc_init_backing(const reist_libc_backing_t *p) {
    validate();
    if (heap_initialized) return -EBUSY;
    if (!p || p->version!=REIST_LIBC_BACKING_VERSION || p->struct_size!=sizeof(*p) ||
        !p->acquire || !p->release || !p->budget || p->budget>REIST_LIBC_PROCESS_LIMIT ||
        p->budget%HEAP_ALIGN || !p->quantum || p->quantum>4U*1024U*1024U ||
        p->quantum%HEAP_ALIGN) return -EINVAL;
    heap_provider=*p;
    provider_check=backing_check();
    heap_initialized=1U;
    return 0;
}
int reist_libc_reset(void) {
    validate();
    if (heap_live) return -EBUSY;
    for (uint32_t i=0;i<REIST_LIBC_BACKING_REGIONS;++i) {
        heap_area_t *a=&heap_areas[i];
        if (!a->base) continue;
        if (heap_provider.acquire) bad_heap(); /* free returned every empty area */
        memset(a->base,0,a->capacity);
    }
    memset(heap_blocks,0,sizeof(heap_blocks));
    memset(heap_areas,0,sizeof(heap_areas));
    memset(&heap_provider,0,sizeof(heap_provider));
    provider_check=0;
    heap_initialized=heap_capacity=heap_live=heap_bytes=heap_peak=0;
    return 0;
}
int reist_libc_stats(reist_libc_stats_t *stats) {
    if (!stats || stats->version!=REIST_LIBC_VERSION ||
        stats->struct_size!=sizeof(*stats)) return -EINVAL;
    validate();
    *stats=(reist_libc_stats_t){REIST_LIBC_VERSION,sizeof(*stats),heap_capacity,
        heap_live,heap_bytes,heap_peak}; return 0;
}
static uint32_t acquire_area(uint32_t span) {
    if (!heap_provider.acquire || span>heap_provider.budget-heap_capacity) return HEAP_END;
    uint32_t area=0, node=vacant_block();
    while (area<REIST_LIBC_BACKING_REGIONS && heap_areas[area].base) ++area;
    if (area==REIST_LIBC_BACKING_REGIONS || node==HEAP_END) return HEAP_END;
    uint32_t capacity=span, quantum=heap_provider.quantum;
    uint32_t remainder=capacity%quantum;
    if (remainder) {
        uint32_t remaining=heap_provider.budget-heap_capacity;
        capacity=quantum-remainder<=remaining-capacity ?
            capacity+quantum-remainder : remaining;
    }
    heap_busy=1;
    void *storage=heap_provider.acquire(heap_provider.context,capacity);
    heap_busy=0;
    if (!storage) return HEAP_END;
    if ((uintptr_t)storage%HEAP_ALIGN || (uintptr_t)storage>UINTPTR_MAX-capacity)
        bad_heap();
    for (uint32_t i=0;i<REIST_LIBC_BACKING_REGIONS;++i) {
        const heap_area_t *a=&heap_areas[i];
        if (a->base && (uintptr_t)storage<(uintptr_t)a->base+a->capacity &&
            (uintptr_t)a->base<(uintptr_t)storage+capacity) bad_heap();
    }
    add_area(area,node,storage,capacity);
    return area;
}
void *malloc(size_t size) {
    validate();
    if (!size) return NULL;
    uint32_t limit=heap_provider.acquire ? heap_provider.budget : REIST_LIBC_HEAP_LIMIT;
    if (!heap_initialized || size>limit || heap_live>=REIST_LIBC_OBJECT_LIMIT) {
        errno=ENOMEM; return NULL;
    }
    uint32_t span=((uint32_t)size+HEAP_ALIGN-1U)/HEAP_ALIGN*HEAP_ALIGN;
    uint32_t selected=HEAP_END, node=HEAP_END;
    for (uint32_t i=0;i<REIST_LIBC_BACKING_REGIONS && node==HEAP_END;++i) {
        if (!heap_areas[i].base) continue;
        for (uint32_t at=heap_areas[i].head;at!=HEAP_END;at=heap_blocks[at].next)
            if (!heap_blocks[at].requested && heap_blocks[at].span>=span) {
                selected=i; node=at; break;
            }
    }
    if (node==HEAP_END) {
        selected=acquire_area(span);
        if (selected==HEAP_END) { errno=ENOMEM; return NULL; }
        node=heap_areas[selected].head;
    }
    heap_block_t *b=&heap_blocks[node];
    if (b->span>span) {
        uint32_t spare=vacant_block();
        /* The metadata bound includes two nodes per live object plus every
         * backing region; a valid partition cannot exhaust it at this point. */
        if (spare==HEAP_END) bad_heap();
        heap_blocks[spare]=(heap_block_t){b->offset+span,b->span-span,0,b->next,0};
        seal(&heap_blocks[spare]); b->next=spare; b->span=span;
    }
    b->requested=(uint32_t)size; seal(b); ++heap_live; heap_bytes+=(uint32_t)size;
    if (heap_bytes>heap_peak) heap_peak=heap_bytes;
    return heap_areas[selected].base+b->offset;
}
void *calloc(size_t count, size_t size) {
    if (size && count>SIZE_MAX/size) { errno=ENOMEM; return NULL; }
    void *p=malloc(count*size); if (p) memset(p,0,count*size); return p;
}
static uint32_t locate(void *p, uint32_t *area, uint32_t *previous) {
    for (uint32_t i=0;i<REIST_LIBC_BACKING_REGIONS;++i) {
        const heap_area_t *a=&heap_areas[i];
        if (!a->base || (uintptr_t)p<(uintptr_t)a->base ||
            (uintptr_t)p-(uintptr_t)a->base>=a->capacity) continue;
        uint32_t prev=HEAP_END;
        for (uint32_t at=a->head;at!=HEAP_END;at=heap_blocks[at].next) {
            if ((uintptr_t)p==(uintptr_t)a->base+heap_blocks[at].offset &&
                heap_blocks[at].requested) {
                if (previous) *previous=prev;
                if (area) *area=i;
                return at;
            }
            prev=at;
        }
    }
    bad_heap(); return HEAP_END;
}
void free(void *p) {
    if (!p) return;
    validate();
    uint32_t prev, area, at=locate(p,&area,&prev);
    heap_block_t *b=&heap_blocks[at];
    memset(p,0,b->requested);
    heap_bytes-=b->requested; --heap_live; b->requested=0;
    if (b->next!=HEAP_END && !heap_blocks[b->next].requested) {
        heap_block_t *next=&heap_blocks[b->next];
        b->span+=next->span; b->next=next->next; memset(next,0,sizeof(*next));
    }
    seal(b);
    if (prev!=HEAP_END && !heap_blocks[prev].requested) {
        heap_blocks[prev].span+=b->span; heap_blocks[prev].next=b->next;
        seal(&heap_blocks[prev]); memset(b,0,sizeof(*b));
    }
    heap_area_t *a=&heap_areas[area];
    b=&heap_blocks[a->head];
    if (heap_provider.acquire && !b->requested && b->span==a->capacity &&
        b->next==HEAP_END) {
        void *storage=a->base; uint32_t capacity=a->capacity;
        heap_capacity-=capacity;
        memset(b,0,sizeof(*b)); memset(a,0,sizeof(*a));
        heap_busy=1;
        heap_provider.release(heap_provider.context,storage,capacity);
        heap_busy=0;
    }
}
void *realloc(void *p, size_t size) {
    if (!p) return malloc(size);
    if (!size) { free(p); return NULL; }
    validate();
    uint32_t at=locate(p,NULL,NULL);
    heap_block_t *b=&heap_blocks[at]; uint32_t old=b->requested;
    if (size<=b->span) {
        heap_bytes=heap_bytes-old+(uint32_t)size;
        b->requested=(uint32_t)size; seal(b);
        if (heap_bytes>heap_peak) heap_peak=heap_bytes;
        return p;
    }
    void *replacement=malloc(size);
    if (!replacement) return NULL;
    memcpy(replacement,p,old); free(p); return replacement;
}
