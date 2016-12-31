
#ifndef SFMM_H
#define SFMM_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define ALLOC_SIZE_BITS 4
#define BLOCK_SIZE_BITS 28
#define UNUSED_SIZE_BITS 28
#define PADDING_SIZE_BITS 4

#define SF_HEADER_SIZE \
    ((ALLOC_SIZE_BITS + BLOCK_SIZE_BITS + UNUSED_SIZE_BITS + PADDING_SIZE_BITS) >> 3)
#define SF_FOOTER_SIZE SF_HEADER_SIZE

/*
                Format of a memory block
    +----------------------------------------------+
    |                  64-bits wide                |
    +----------------------------------------------+

    +----------------+------------+-------+--------+
    |  Padding Size| _Unused  | Block Size |  000a | <- Header Block
    |    in bytes  |  28 bits |  in bytes  |       |
    |     4 bits   |          |   28 bits  | 4 bits|
    +--------------+----------+------------+-------+
    |                                              | Content of
    |         Payload and Padding                  | the payload
    |           (N Memory Rows)                    |
    |                                              |
    |                                              |
    +---------------+---------------------+--------+
    |     Unused    | Block Size in bytes |   000a | <- Footer Block
    +---------------+---------------------+--------+

*/
struct __attribute__((__packed__)) sf_header {
    uint64_t alloc : ALLOC_SIZE_BITS;
    uint64_t block_size : BLOCK_SIZE_BITS;
    uint64_t unused_bits : UNUSED_SIZE_BITS;
    uint64_t padding_size : PADDING_SIZE_BITS;
};

typedef struct sf_header sf_header;

struct __attribute__((__packed__)) sf_free_header {
    sf_header header;
    /* Note: These next two fields are only used when the block is free.
     *       They are not part of header, but we place them here for ease
     *       of access.
     */
    struct sf_free_header *next;
    struct sf_free_header *prev;
};
typedef struct sf_free_header sf_free_header;

struct __attribute__((__packed__)) sf_footer {
    uint64_t alloc : ALLOC_SIZE_BITS;
    uint64_t block_size : BLOCK_SIZE_BITS;
    /* Other 32-bits are unused */
};
typedef struct sf_footer sf_footer;

typedef struct {
    size_t internal;
    size_t external;
    size_t allocations;
    size_t frees;
    size_t coalesce;
} info;

extern sf_free_header *freelist_head;


void *sf_malloc(size_t size);


void *sf_realloc(void *ptr, size_t size);


int sf_info(info *meminfo);


void sf_free(void *ptr);


void sf_mem_init();


void sf_mem_fini();

void *sf_sbrk();


void sf_snapshot(bool verbose);


void sf_blockprint(void *block);


void sf_varprint(void *data);

#endif