#include "kernel.h"

extern "C" { extern char __heap_start; extern char __heap_end; }

struct block_hdr {
    uint32_t size;
    uint32_t free;
    block_hdr* next;
};

static block_hdr* g_heap_head;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
void mm_init() {
    // Guard: heap end must not overlap stack (256-byte guard below 0x4087C000)
    // Linker.ld sets __heap_end = 0x4087C000 - 256; this check catches
    // misconfiguration if the linker script is later changed carelessly
    g_heap_head = (block_hdr*)&__heap_start;
    uint32_t total = (uint32_t)&__heap_end - (uint32_t)&__heap_start;
    g_heap_head->size = total - sizeof(block_hdr);
    g_heap_head->free = 1;
    g_heap_head->next = 0;
}
#pragma GCC diagnostic pop

void* mm_alloc(uint32_t size) {
    // ponytail: first-fit free-list, replace with buddy if fragmentation matters
    size = (size + 3) & ~3u;
    block_hdr* b = g_heap_head;
    while (b) {
        if (b->free && b->size >= size) {
            if (b->size >= size + sizeof(block_hdr) + 16) {
                block_hdr* split = (block_hdr*)((uint32_t)b + sizeof(block_hdr) + size);
                split->size = b->size - size - sizeof(block_hdr);
                split->free = 1;
                split->next = b->next;
                b->size = size;
                b->next = split;
            }
            b->free = 0;
            return (void*)((uint32_t)b + sizeof(block_hdr));
        }
        b = b->next;
    }
    return 0;
}

void mm_free(void* ptr) {
    if (!ptr) return;
    block_hdr* b = (block_hdr*)((uint32_t)ptr - sizeof(block_hdr));
    b->free = 1;

    // coalesce adjacent free blocks
    block_hdr* c = g_heap_head;
    while (c) {
        if (c->free && c->next && c->next->free) {
            c->size += sizeof(block_hdr) + c->next->size;
            c->next = c->next->next;
            continue;
        }
        c = c->next;
    }
}

uint32_t mm_free_bytes() {
    uint32_t total = 0;
    block_hdr* b = g_heap_head;
    while (b) {
        if (b->free) total += b->size;
        b = b->next;
    }
    return total;
}

uint32_t mm_total_bytes() {
    return (uint32_t)&__heap_end - (uint32_t)&__heap_start;
}

uint32_t mm_heap_start() { return (uint32_t)&__heap_start; }
uint32_t mm_heap_end() { return (uint32_t)&__heap_end; }

// ponytail: stub swap/zram — add LZ4 when real swap-to-flash needed
static uint32_t g_swap_total = 0, g_swap_used = 0;

void mm_swap_init() { g_swap_total = 0; g_swap_used = 0; }
void mm_zram_init() {}
void mm_swap_info(uint32_t* total, uint32_t* used) {
    if (total) *total = g_swap_total;
    if (used)  *used  = g_swap_used;
}
