#ifndef MALLOC_H
#define MALLOC_H

#include <stddef.h>
#include<unistd.h>

#define BITMAP_SIZE 128
#define  HEADER_SIZE sizeof(page_header)
#define PAGE_SIZE 4096
#define FREE_LIST_SIZE 13
#define FREE_POOL 12

#define LARGE_PAGE_MARKER 0x8000
#define FIRST_DATA_BLOCK(base_ptr, size) (void *)((int)base_ptr+(size-(HEADER_SIZE%size)+HEADER_SIZE))
#define BLOCKNO2PTR(num, base_ptr, size) (void *)((int)FIRST_DATA_BLOCK(base_ptr, size) + num*size)
#define NUMBLOCKS(size) ((PAGE_SIZE-(int)(FIRST_DATA_BLOCK(NULL,size)))/size)
#define SET_BITMAP(bitmap, blk, num) (bitmap[blk] |= num)
#define BLOCKNO(ptr,size) (((int)ptr-(int)FIRST_DATA_BLOCK(ROUNDDOWN(ptr),size))/size)
#define ROUNDDOWN(addr) (void *)(((int)addr)&(~(PAGE_SIZE-1)))
#define INDEX2SIZE(index) (1<<index)
#define BLOCKNO2BIT(blockno) (1<<(blockno%8))
#define RESET_BITMAP(bitmap, blkno) (bitmap[blkno/8] &= ~BLOCKNO2BIT(blkno))

void* malloc(size_t size);
void* calloc(size_t number, size_t size);
void* realloc(void *ptr, size_t size);
void free(void* ptr);

typedef struct Node1
{
    //32 bit or 4 byte alligned structure
    short block_size;          //size of block to allocate in the page
    short allocated_blocks;     //number of blocks allocated
    struct Node* next_free;         //pointer to meta-data of next free page
    char bitmap[BITMAP_SIZE];  //blocks free in the current page
    //struct Node* prev; //pointer to prev meta-data on the heap
}page_header;

typedef struct Entry 
{
    page_header *free_start;
    page_header *free_end;
}free_entry;

#endif /*MALLOC_H*/
