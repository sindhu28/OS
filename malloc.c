#include <errno.h>
#include <limits.h>

#include "malloc.h"
#include "memreq.h"

static int total_free = 0;
static __thread free_entry free_list[FREE_LIST_SIZE];

size_t get_index(size_t size) 
{
    size_t index, s;
    //get index of the free list array for the allocation size
    index = 0;
    s = size;
    while((s=(s>>1))) 
    {
        index++;
    }
    if((size%(1<<index)))
        index++;
    return LISTINDEX(index);
}

void manage_free_pool(page_header *temp_ptr, free_entry *free_pool, page_header *prev_ptr)
{
    page_header *ptr;
    //reset pointers in th free list and free pool when a page is allocated from pages in free pool
    if(temp_ptr == free_pool->free_start)
    {
         free_pool->free_start = (page_header*)temp_ptr->next_free;
         if(free_pool->free_start == NULL)
         { 
             free_pool->free_end = NULL;
         }
    }
    else
    {
        ptr = prev_ptr;
        ptr->next_free = temp_ptr->next_free;
        if(ptr->next_free == NULL)
            free_pool->free_end = ptr;
    }
}

void manage_large_page_free_pool(page_header *temp_ptr, int pages, page_header *prev_ptr, int count)
{
    page_header *start_ptr = NULL;
    page_header *end_ptr = NULL;
    page_header *ptr = NULL;
    free_entry *free_pool = NULL;

    //reset pointers in th free pool when a large page is allocated from pages in free pool
    free_pool = &free_list[FREE_POOL];
    if(prev_ptr == NULL)
        start_ptr = (page_header*)((char*)temp_ptr-(count*PAGE_SIZE));
    else
        start_ptr = (page_header*)prev_ptr->next_free;
    end_ptr = (page_header*)((char*)start_ptr+((pages-1)*PAGE_SIZE));

    if(start_ptr == free_pool->free_start)
    {
         free_pool->free_start = (page_header*)end_ptr->next_free;
         if(free_pool->free_start == NULL)
         {
             free_pool->free_end = NULL;
         }
    }
    else
    {
        ptr = prev_ptr;
        ptr->next_free = end_ptr->next_free;
        if(ptr->next_free == NULL)
            free_pool->free_end = ptr;
    }
}

int get_free_block(page_header *page_hdr, size_t size) 
{
    int blockno=0, bn;
    int block, bit, mask;
    char *bitmap = page_hdr->bitmap;
    int total_blocks = NUMBLOCKS(size);
    //from the bitmap in header, find the first free block for the allocation size
    for(block=0; block<BITMAP_SIZE; block++) 
    {
        bn = bitmap[block];
        for(bit=0; bit<8; bit++) 
        {
            if(total_blocks == 0)
                    return -1;
            if(!(bn&(1<<bit))) 
            {
                blockno = block*8+bit;
                mask = 1<<bit;
                SET_BITMAP(page_hdr->bitmap, block, mask);
                return blockno;
            }
            total_blocks--;
        }
    }
    return -1;  
}

void *malloc(size_t size) 
{
    free_entry *entry = NULL;
    free_entry *free_pool = NULL;
    page_header *temp_ptr = NULL;
    page_header *new_page;
    page_header *prev_ptr = NULL;
    page_header *ptr = NULL;
    large_page_header *allocate_ptr = NULL;
    size_t index;
    int  blockno = 0;
    void *ret_addr = NULL;
    int pages, count = 0;
    
    //Small page size requests : Page size : powers of 2 till PAGE_SIZE/2
    if(size <= PAGE_SIZE/2) 
    {
        index = get_index(size);  //index in free list to allocate
        size = 1<<index;          //size to allocate to user (powers of 2)
        entry = &free_list[index];

        //check if there is any free page in the list to allocate
        if(entry->free_start != NULL) 
        {
            if(blockno >= 0)
                ret_addr = BLOCKNO2PTR(blockno, entry->free_start, size);
            //check if this allocation makes this page full
        }
        else
        {
            //check if free page is available in free pool
            free_pool = &free_list[FREE_POOL];
            ret_addr = NULL;
            if(free_pool->free_start != NULL)
            {
                //allocate a page from free pool
                temp_ptr = free_pool->free_start;
                while(temp_ptr != NULL)
                {   
                    if((((char*)temp_ptr)+PAGE_SIZE) ==(char*) (temp_ptr->next_free))
                    {
                        count++;
                    }
                    else
                    {
                        if(count == 0)
                        {
                            manage_free_pool(temp_ptr, free_pool, prev_ptr);
                            //allocate this page
                            temp_ptr->block_size = size;
                            temp_ptr->next_free = NULL;
                            temp_ptr->allocated_blocks = 0;
                            entry->free_start = temp_ptr;
                            entry->free_end = temp_ptr;
                            blockno = get_free_block(entry->free_start, size);
                            if(blockno >= 0)
                                ret_addr = BLOCKNO2PTR(blockno, entry->free_start, size);
                            break;
                        }
                        else
                        {
                            count = 0;
                        }
                    }
                    prev_ptr = temp_ptr;
                    temp_ptr = (page_header*)temp_ptr->next_free;
                }
                if(!ret_addr)
                {
                    temp_ptr = free_pool->free_start;
                    free_pool->free_start = (page_header*)temp_ptr->next_free;
                    if(free_pool->free_start == NULL)
                        free_pool->free_end = NULL;
                    //allocare the first entry
                    temp_ptr->block_size = size;
                    temp_ptr->next_free = NULL;
                    temp_ptr->allocated_blocks = 0;
                    entry->free_start = temp_ptr;
                    entry->free_end = temp_ptr;
                    blockno = get_free_block(entry->free_start, size);
                    if(blockno >= 0)
                        ret_addr = BLOCKNO2PTR(blockno, entry->free_start, size);
                }
            }
            else
            {
                //allocate a new page using system call
                new_page = (page_header *)get_memory(PAGE_SIZE);
                if(new_page == NULL)
                    return;
                if(new_page)
                {
                    memset(new_page, 0, sizeof(page_header));
                    new_page->block_size = size;
                    new_page->next_free = NULL;
                    new_page->allocated_blocks = 0;
                    entry->free_start = new_page;
                    entry->free_end = new_page;
                    blockno = get_free_block(entry->free_start, size);
                    if(blockno >= 0)
                        ret_addr = BLOCKNO2PTR(blockno, entry->free_start, size);
                }
                else
                {
                    errno = ENOMEM;
                    printf("ENOMEM\n");
                    return NULL;
                }
            }
        }

        if(entry->free_start && ret_addr) 
        {
            entry->free_start->allocated_blocks++;
            if (entry->free_start->allocated_blocks == NUMBLOCKS(size)) 
            {
                //remove block from free list if it is full
                temp_ptr = entry->free_start;
                entry->free_start = (page_header*)temp_ptr->next_free;
                if (entry->free_start == NULL)
                    entry->free_end = NULL;
                temp_ptr->next_free = NULL;
            }
        }
        return ret_addr;
    }
    else 
    {

        //long pages
        free_pool = &free_list[FREE_POOL];
        ret_addr = NULL;
        if(((size+LARGE_HEADER_SIZE)%PAGE_SIZE))
            pages = ((size+LARGE_HEADER_SIZE)/PAGE_SIZE) + 1;
        else
            pages = (size+LARGE_HEADER_SIZE)/PAGE_SIZE;
        //check if you can find a page in the free pool
        if(free_pool->free_start != NULL)
        {
            //allocate a page from free pool
            temp_ptr = free_pool->free_start;
            prev_ptr = NULL;
            while(temp_ptr != NULL)
            {
                if((((char*)temp_ptr)+PAGE_SIZE) == (char*)(temp_ptr->next_free))
                {
                    count++;
                }
                else
                {

                    if(count >= (pages-1))
                    {
                        if(prev_ptr == NULL)
                            ptr = (page_header*)((char*)temp_ptr-(count*PAGE_SIZE));
                        else
                            ptr = (page_header*)prev_ptr->next_free;

                        manage_large_page_free_pool(temp_ptr, pages, prev_ptr, count);
                        //allocate this page
                        //add large page marker
                        allocate_ptr = (large_page_header*)ptr;
                        allocate_ptr->block_size = 0x8000;
                        allocate_ptr->allocated_blocks = pages;
                        ret_addr = (large_page_header*)allocate_ptr+1;
                        break;
                    }
                    else
                    {
                        prev_ptr = temp_ptr;
                        count = 0;
                    }
                }
                temp_ptr = (page_header*)temp_ptr->next_free;
            }
        }
        if(ret_addr == NULL)
        {
            //allocate a new page using system call
            new_page = (large_page_header *)get_memory(pages*PAGE_SIZE);
            if(new_page)
            {
                memset(new_page, 0, sizeof(page_header));
                //add large page marker
                new_page->block_size = 0x8000;
                new_page->allocated_blocks = pages;
                ret_addr = (large_page_header*)new_page+1;
            }
            else
            {
                errno = ENOMEM;
                printf("ENOMEM\n");
                return NULL;
            }
        }
        return ret_addr; 
    }
}

static size_t highest(size_t in) {
    size_t num_bits = 0;

    while (in != 0) {
        ++num_bits;
        in >>= 1;
    }

    return num_bits;
}

void* calloc(size_t number, size_t size) {
    size_t number_size = 0;

    /* This prevents an integer overflow.  A size_t is a typedef to an integer
     * large enough to index all of memory.  If we cannot fit in a size_t, then
     * we need to fail.
     */
    if (highest(number) + highest(size) > sizeof(size_t) * CHAR_BIT)
    {
        errno = ENOMEM;
        return NULL;
    }

    number_size = number * size;
    void* ret = malloc(number_size);

    if (ret)
    {
        memset(ret, 0, number_size);
    }

    return ret;
}

void* realloc(void *ptr, size_t size) {
    page_header *page_base  = ROUNDDOWN(ptr);
    size_t old_size = 0;
    if ((size&LARGE_PAGE_MARKER) == 0)
        old_size = page_base->block_size; /* XXX Set this to the size of the buffer pointed to by ptr */
    else
        old_size = page_base->allocated_blocks*PAGE_SIZE;
    void* ret = malloc(size);
    if (ret) {
        if (ptr) {
            memmove(ret, ptr, old_size < size ? old_size : size);
            free(ptr);
        }

        return ret;
    } else {
        errno = ENOMEM;
        return NULL;
    }
}

void manage_free_list(size_t size, page_header *page_base)
{
    page_header *ptr;
    page_header *prev_ptr = NULL;
    free_entry *curr_list;

    //Rearrange pointers in the free list when a page is removed from the list
    curr_list = &free_list[get_index(size)];
    ptr = curr_list->free_start;
    while(ptr != NULL)
    {
        if(ptr == page_base)
            break;
        prev_ptr = ptr;
        ptr = (page_header*)ptr->next_free;
    }
    if(prev_ptr == NULL)
    {
        curr_list->free_start = (page_header*)ptr->next_free;
        if(curr_list->free_start == NULL)
            curr_list->free_end = NULL;
    }
    else
    {
        prev_ptr->next_free = ptr->next_free;
        if(prev_ptr->next_free == NULL)
            curr_list->free_end = prev_ptr;
    }
    ptr->next_free = NULL;
}

void split_to_pages(page_header* page_base)
{
    page_header *new_ptr;
    page_header *temp_ptr;
    page_header *next_ptr;
    free_entry *free_pool;
    int pages;

    //split allocation of chunks of pages in individual pages when freed and add to free pool
    free_pool = &free_list[FREE_POOL];
    next_ptr = (page_header*)page_base->next_free;
    pages = page_base->allocated_blocks;
    temp_ptr = page_base;
    while (pages!= 1)
    {
        new_ptr = (page_header*)((char*)temp_ptr + PAGE_SIZE);
        temp_ptr->next_free = new_ptr;
        //removing large page marker
        temp_ptr->block_size = 0;
        temp_ptr->allocated_blocks = 0;
        temp_ptr->next_free = new_ptr;
        temp_ptr = new_ptr;
        pages--;
    }
    temp_ptr->next_free = next_ptr;
    //reset start and end of free pool
    if(page_base == free_pool->free_end)
    {
        free_pool->free_end = temp_ptr;
    }
}


void free(void* ptr) {
    page_header *page_base;
    page_header *swap;
    free_entry *free_pool = NULL;
    page_header *temp_ptr;
    page_header *prev_ptr;
    size_t size = 0;
    int blockno;
    
    if(ptr == NULL)
        return;

    //find the page base address from user given pointer
    page_base = (page_header*)ROUNDDOWN(ptr);
    if (!page_base)
        return;

    free_pool = &free_list[FREE_POOL];
    size  = page_base->block_size;
    total_free++;
    //check if small page allocation is being freed
    if ((size&LARGE_PAGE_MARKER) == 0) 
    {
        blockno = BLOCKNO(ptr, size);
	RESET_BITMAP(page_base->bitmap, blockno);

        if (page_base->allocated_blocks == 1) 
        {
            //add to free pool if page is going to get completely empty
            //add the page in sorted order into free pool if page is empty
            //remove from free list if page is on free list for that allocation size
            if(NUMBLOCKS(size)!=1)
            {
                //Rearrange pointers in the free list when a page is removed
                manage_free_list(size, page_base);
            } 

            temp_ptr = free_pool->free_start;
            while(temp_ptr != NULL)
            {
                //adding to free pool in sorted order of address
                if(page_base < temp_ptr)
                {
                    if(temp_ptr == free_pool->free_start)
                    {
                        page_base->next_free = free_pool->free_start;
                        free_pool->free_start = page_base;
                    }
                    else
                    {
                        page_base->next_free = temp_ptr;
                        prev_ptr->next_free = page_base;
                    }
                    break;
                }
                prev_ptr = temp_ptr;
                temp_ptr = (page_header*)temp_ptr->next_free;
            }
            if(!(temp_ptr))
            {
                if(free_pool->free_start == NULL)
                {
                    free_pool->free_start = page_base;
                    free_pool->free_end = page_base;
                    page_base->next_free = NULL;
                }
                else
                {
                    free_pool->free_end->next_free = page_base;
                    page_base->next_free = NULL;
                    free_pool->free_end = page_base;
                }
            }
        }
        else if (page_base->allocated_blocks == NUMBLOCKS(size)) 
        {
            //If block of full previously and this free is making it partially empty, add it to the free list for that allocation size
            size = get_index(size);
            if(free_list[size].free_end == NULL) 
            {
                free_list[size].free_end    = page_base;
                free_list[size].free_start  = page_base;
            }
            else 
            {
                swap = free_list[size].free_end;
                page_base->next_free = NULL;
                free_list[size].free_end = page_base;
                swap->next_free  = page_base;
            }
        }
        else
        {
            //do nothing
        }
        page_base->allocated_blocks--;
    }  
    else
    {
        //freeing large page requests
        //split large pages into individual pages and add to free pool in sorted order
        return;
        temp_ptr = free_pool->free_start;
        prev_ptr = NULL;

        while(temp_ptr != NULL)
        {
            //adding to free pool in sorted order
            if(page_base < temp_ptr)
            {
                if(temp_ptr == free_pool->free_start)
                {
                    page_base->next_free = free_pool->free_start;
                    free_pool->free_start = page_base;
                }
                else
                {
                    page_base->next_free = temp_ptr;
                    prev_ptr->next_free = page_base;
                }
                break;
            }
            prev_ptr = temp_ptr;
            temp_ptr = (page_header*)temp_ptr->next_free;
        }
        if(!(temp_ptr))
        {
            if(free_pool->free_start == NULL)
            {
                free_pool->free_start = page_base;
                free_pool->free_end = page_base;
                page_base->next_free = NULL;
            }
            else
            {
                free_pool->free_end->next_free = page_base;
                page_base->next_free = NULL;
                free_pool->free_end = page_base;
            }
        }

        //split the page and remove large page marker
        split_to_pages(page_base);
    }
}
