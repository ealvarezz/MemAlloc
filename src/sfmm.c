#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "sfmm.h"

/**
 * All functions you make for the assignment must be implemented in this file.
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */

 #define SHIFT(p)((p) >> 4)
 #define SHIFT_BACK(p)((p) << 4)
 #define GET_PAYLOAD(block_size)((block_size) - 16)
 #define GO_TO_FOOTER(adress, block_size)((adress) + GET_PAYLOAD(block_size) + 8)
 #define BACK_TO_HEADER(adress, block_size)((adress) - GET_PAYLOAD(block_size) - 8)
 #define CREATE_PADDING(size)(((size) % 16 == 0) ? (size) : (size) + ((size) % 16))
 #define GO_UP(address)((void*)(address) + 8)
 #define GO_DOWN(address)((void*)(address) - 8)
 #define PAGE_SIZE 4096
 #define SMALLEST_BLOCK_SIZE 32

sf_free_header* freelist_head = NULL;
static unsigned int internal = 0;
static unsigned int external = 0;
static unsigned int alllocations = 0;
static unsigned int frees = 0;
static unsigned int coalesces = 0;
static int pages_used = 0;
static void* upper_bound;
static void* lower_bound;

void push(sf_free_header* new_free_block){
	if(freelist_head == NULL){
		freelist_head = new_free_block;
		freelist_head->next = NULL;
		freelist_head->prev = NULL;
		return;
	}

	freelist_head->prev = new_free_block;
	new_free_block->next = freelist_head;
	new_free_block->prev = NULL;
	freelist_head = new_free_block;
}

void remove_from_list(sf_free_header* free_block){

	// *********** remove this at the end **************//
	if(free_block == NULL)
		fprintf(stderr,"Can't remove a free list!\n");

	// Remove the payload size from external
	external -= SHIFT_BACK(free_block->header.block_size);

	// This means there's only the head in the list
	if(free_block->prev == NULL && free_block->next == NULL)
		freelist_head = NULL;

	// This means that we're looking at the head of the list
	if(free_block->prev == NULL && free_block->next != NULL){
		free_block = free_block->next;
		free_block->prev = NULL;
		freelist_head = free_block;
	}
	// This will mean that we're in the tail of the list
	else if(free_block->prev != NULL && free_block->next == NULL){
		free_block = free_block->prev;
		free_block->next = NULL;
	}
	// This means we're in the middle of our list
	else if(free_block->prev != NULL && free_block->next != NULL){
		free_block = free_block->prev;
		free_block->next = free_block->next->next;
		free_block = free_block->next;
		free_block->prev = free_block->prev->prev;
	}
}

void update_header(sf_header* header, 
	uint64_t new_alloc, uint64_t new_block_size, uint64_t new_padding){
	header->alloc = new_alloc;
	header->block_size = new_block_size;
	header->padding_size = new_padding;
}

void update_footer(sf_footer* footer, uint64_t new_alloc, uint64_t new_block_size){
	footer->alloc = new_alloc;
	footer->block_size = new_block_size;
}

void coalesce(sf_free_header* free_header){
	// The free header passed to this function has already been set to not
	// allocated and has been removed from the linked list

	sf_free_header* current_free_header;
	sf_header* current_header;
	sf_footer* current_footer;
	int current_block_size = SHIFT_BACK(free_header->header.block_size);
	bool did_coalesce = false;

	// Checking the previous block
	current_footer = (sf_footer*) GO_DOWN(free_header);
	if((void*)free_header > lower_bound && current_footer->alloc == 0){
		did_coalesce = true;
	// This means that the previous block is empty so it must be coalesed with
	// this block
		current_header = BACK_TO_HEADER((void*) current_footer, 
			SHIFT_BACK(current_footer->block_size));
	// You must now remove this free header form the linked list
		current_free_header = (sf_free_header*) current_header;
		remove_from_list(current_free_header);

	// You add the current block size into this block
		current_header->block_size = SHIFT(SHIFT_BACK(current_header->block_size) 
			+ current_block_size);
	// This is gonna jump to the footer of the block that's being freed
		current_footer = GO_TO_FOOTER((void*)current_header, 
			SHIFT_BACK(current_header->block_size));
		current_footer->block_size = current_header->block_size;

	// update the current block size
		current_block_size = SHIFT_BACK(current_footer->block_size);

	}else{
		current_footer = GO_TO_FOOTER((void*)free_header, current_block_size);
	}

	// Here we add 8 to the current footer to go to the next block to check if
	// it's allocated
	current_header = (sf_header*)GO_UP(current_footer);
	if((void*)current_footer < upper_bound && current_header->alloc == 0){
		did_coalesce = true;
	// You must now remove this free header form the linked list
		current_free_header = (sf_free_header*) current_header;
		remove_from_list(current_free_header);

	// This means that the next block was not allocated
		current_footer = GO_TO_FOOTER((void*)current_header, 
			SHIFT_BACK(current_header->block_size));
		current_footer->block_size = SHIFT(SHIFT_BACK(current_footer->block_size) 
			+ current_block_size);

	// Now the new footer is updated, the new header has to be updated too
		current_header = BACK_TO_HEADER((void*) current_footer, 
			SHIFT_BACK(current_footer->block_size));
		current_header->block_size = current_footer->block_size;

	// We set the current header as a free list with all the blocks coalesed
		free_header = (sf_free_header*)current_header;

	// Update the current block size
		current_block_size = SHIFT_BACK(current_header->block_size);

	}else{
	// If it's not allocated just set the header of the combined blocks to a
	// a free block in order to be pushed into the free list
		free_header = BACK_TO_HEADER((void*) current_footer, 
			SHIFT_BACK(current_footer->block_size));
	}

	// Adding the free memory that will now be available minus the header and
	// the footer
	external += current_block_size;

	if(did_coalesce)
		coalesces++;

	push(free_header);


}

int init_new_page(){
	int current_block_size = SHIFT(PAGE_SIZE);
	sf_free_header* curent_free_header;

	// Initializes new memory for the freelist if it's set to null
	curent_free_header = sf_sbrk(0);

	if(sf_sbrk(1) == (void*)-1)
		return-1;

	if(pages_used == 0)
		lower_bound = curent_free_header;
	curent_free_header->header.alloc = 0;
	curent_free_header->header.block_size = current_block_size;
	curent_free_header->header.padding_size = 0;
	pages_used++;

	// Now the footer must be created as well for this new block
	sf_footer* footer = GO_TO_FOOTER(((void*)curent_free_header), 
		SHIFT_BACK(current_block_size));
	footer->alloc = 0;
	footer-> block_size = current_block_size;
	upper_bound = footer;

	// Now go back to the header and check if the there was a head previously
	// if so change that head's prev to this one
	coalesce(curent_free_header);

	return 0;

}

void *sf_malloc(size_t size){

	int current_block_size;
	char padding;
	bool splitting = true;
	sf_free_header* current_free_header = freelist_head;
	sf_header* current_header;
	sf_footer* current_footer;

	if(size == 0)
		return NULL;
	

	// Handle padding issues
	if(size % 16 != 0){
		padding = 16 - (size % 16);
		size += padding;
	}
	else
		padding = 0;

	// If there is no space currently avaliable in the free llist then this 
	// function is called to inintialize a new page with sbrk and adding
	// it into the free list ready to allocate
	if(current_free_header == NULL && init_new_page())
		if(init_new_page() == -1){
			errno = ENOMEM;
  			return NULL;
		}

	// It will keep interating in case that no free block is found such that
	// could store the requested size. It will call init_new_page() and add the
	// new block to the array list and interate again through the free lists. If 
	// sbrk was called four times, at the fifth call it will return a -1 and breal
	// from this loop, this means that the memory can't allocate this element.
	do{
		current_free_header = freelist_head;
	// Now we must start looking for a free block that can store size_t 
	do{
		current_block_size = current_free_header->header.block_size;
		current_block_size = SHIFT_BACK(current_block_size);

		if(current_block_size >= (size + 16)){
			alllocations++;
	// If this happens then the block is in the range from the perfect size
	// required to store this element or the free block is bigger but the remaining
	// space is not bigger than 32 which then it's not big enough to become a free
	// block. In this case we would take the entire free block with size becoming
	// the current block size minus the header and footer.
			if(current_block_size < size + 48){
				size = current_block_size - 16;
				splitting = false;
			}
	// This replaces the old header with the new header for the allocated block
			remove_from_list(current_free_header);
			current_header = (sf_header*) &(current_free_header->header);
			update_header(current_header, 1, SHIFT(size + 16), padding);

	// This will replace the old footer with the new footer for the new allocated block
			current_footer = (sf_footer*) GO_TO_FOOTER((void*)current_header,size + 16);
			update_footer(current_footer, 1, SHIFT(size + 16));
			internal += padding + 16;
			//external -= current_block_size;

	// The block doesn't need to be split, then then malloc returns. The splinters 
	// were already taken cared of by updating the size accordingly
			if(!splitting)
				return GO_UP(current_header);

	// Now the rest of the block must become a new free block to be places
	// at the stack
	// Now the footer + 8 will be the header of a brand new free block
			current_free_header = (sf_free_header*) GO_UP(current_footer);
			int new_block_size = current_block_size - SHIFT_BACK(current_footer->block_size);
			update_header(&current_free_header->header, 0, SHIFT(new_block_size), 0);
			current_footer = GO_TO_FOOTER((void*)current_free_header, new_block_size);
			update_footer(current_footer, 0, SHIFT(new_block_size));
			coalesce(current_free_header);
			return GO_UP(current_header);
		}


	}while((current_free_header = current_free_header->next) != NULL);


	splitting = true;
	}while(init_new_page() != -1);

	errno = ENOMEM;
  	return NULL;
}

bool valid_address(void* ptr){
	// This function will check the alloc and block size of the header and footer
	// and check if they are equal, if they're not then this means that this is
	// not an address that's at the beggining of the payload
	sf_header* current_header = GO_DOWN(ptr);
	sf_footer* current_footer = GO_TO_FOOTER((void*)current_header,
		SHIFT_BACK(current_header->block_size));

	if(current_header->alloc != current_footer->alloc)
		return false;
	else if(current_header->block_size != current_footer->block_size)
		return false;

	// We return true if it all matches then we know the address is at the
	// beggining of the payload
	return true;
}

bool already_free(void* ptr){
	sf_header* current_header = GO_DOWN(ptr);
	sf_footer* current_footer = GO_TO_FOOTER((void*)current_header,
		SHIFT_BACK(current_header->block_size));

	if(current_header->alloc == 0 && current_footer->alloc == 0)
		return true;
	else
		return false;
}

void sf_free(void *ptr){

	// TODO: Find out what to do if you pass NULL
	// check if the allocated bit is set in the header and footer 
	// and compare the block size.
	if(ptr == NULL)
		return;

	// Find out if the adress it's in the bounds of the heap
	// This means that this address it's not within our heap space
	if(ptr > (upper_bound - 16) || ptr <= lower_bound || !valid_address(ptr)){
		errno = EINVAL;
		return;
	}

	if(ptr == NULL || already_free(ptr))
		return;

	// Update the header
	sf_free_header* free_header = (sf_free_header*) (ptr - 8);
	internal -= (free_header->header.padding_size + 16);
	free_header->header.alloc = 0;
	free_header->header.padding_size = 0;


	// Update the footer
	sf_footer* footer = GO_TO_FOOTER((void*)free_header,
		SHIFT_BACK(free_header->header.block_size));
	footer->alloc = 0;

	// Coealesce with the adjecent free blocks
	coalesce(free_header);
	frees++;

}

void shrink(sf_header* header, sf_footer* footer, int current_size, 
	int requested_size, int padding){
	int new_block_size;

	// This will shirk the block thus thrunkating any data that's leftover
	internal -= header->padding_size;
	internal += padding;
	update_header(header, 1, SHIFT(requested_size), padding);
	footer = GO_TO_FOOTER((void*)header, requested_size);
	update_footer(footer, 1, SHIFT(requested_size));

	// This will create a new free block from the free space that was remaining
	// If the prigram goes inside this functin this free space is guranteed to be
	// greater or equal for 32

	new_block_size = current_size - requested_size;
	header = GO_UP(footer);
	update_header(header, 0, SHIFT(new_block_size), 0);
	footer = GO_TO_FOOTER((void*)header, new_block_size);
	update_footer(footer, 0, SHIFT(new_block_size));

	// Now you must coalesce this new free block and ass it to the list
	sf_free_header* free = (sf_free_header*)header;
	coalesce(free);

}

void* expand(void* ptr, int size){
	void* new_pointer;
	// Inside this function we're guaranteed that the requested size
	// exeeds any over allocation and padding the current block has plus
	// there's no free space adjacent to this block that could be used
	// for this re-allocation
	sf_header* header = GO_DOWN(ptr);
	new_pointer = sf_malloc(size);
	if(new_pointer == NULL)
		return NULL;

	memcpy(new_pointer, ptr, GET_PAYLOAD(SHIFT_BACK(header->block_size)));
	sf_free(ptr);

	// Now the address at the start of the payload is returned
	// Both header and footer are already taken cared of by malloc all that's
	// left to do is return;
	return new_pointer;

}

void *sf_realloc(void *ptr, size_t size){
	sf_header* current_header;
	sf_footer* current_footer = NULL;
	int padding, or_request_size, or_header_size, combined_size, or_padding_size;
	// Save the original size for future reference in case we need to call
	// malloc with the original size
	or_request_size = size;

	// This takes care of the base cases
	// **************** check for erno numbers *******************
	// In case they pass a pointer thats ex. 5 bytes below the upper_bound
	if(ptr > (upper_bound - 16) || ptr <= lower_bound){
		errno = EINVAL;
		return NULL;
	}
	else if(size == 0){
		errno = EINVAL;
		return NULL;
	}
	else if(ptr == NULL)
		return sf_malloc(size);

	// If they call realloc for the same size then it will return the
	// same pointer since there's nothing else to do
	current_header = GO_DOWN(ptr);
	or_header_size = SHIFT_BACK(current_header->block_size);
	or_padding_size = current_header->padding_size;
	if(size == or_header_size - 16)
		return ptr;

	// Handle padding issues
	if(size % 16 != 0){
		padding = 16 - (size % 16);
		size += padding + 16;
	}
	else{
		padding = 0;
		size +=16;
	}

	// At this point size contains the new block size that will be needed
	// to hold the requested size
	if(size == or_header_size){
	// You only need to update the padding and return the same pointer
		internal -= (or_padding_size - padding);
		current_header->padding_size = padding;
		return ptr;
	}
	else if(or_header_size > size){
		if((or_header_size - size) >= SMALLEST_BLOCK_SIZE){
			shrink(current_header, current_footer, or_header_size, size, padding);
			return ptr;
		}
		else{
			sf_header* or_header = current_header;
			current_header = GO_UP(GO_TO_FOOTER((void*)current_header, or_header_size));
			if((or_header_size - size) >= 16 && (void*)current_header < upper_bound && current_header->alloc == 0){
	// Right now your footer should be at the footer address of the new combined block
	// just go up to get the next header
				shrink(or_header, current_footer, or_header_size, size, padding);
				return ptr;
						
			}

			internal -= (or_padding_size - padding);
			external += (or_header_size - size);
			or_header->padding_size = padding;
			return ptr;
		}
	}
	else if(size > or_header_size){
	// Go to the head of the next block to see if it's not allocated
	// Then you must check if youre not beyond the upper bound and that the block
	// is not allocated and it's less than upper_bound because if there is a block
	// there it must be 32 bytes or more
		current_header = GO_UP(GO_TO_FOOTER((void*)current_header, or_header_size));
		if(current_header->alloc == 0 && (void*)current_header < upper_bound){
			combined_size = or_header_size + SHIFT_BACK(current_header->block_size);
			if(combined_size >= size){
	// The combined size id big enough to handle the reallocation to a bigger memory
	// space. We go back to the header of the current block and update then we go to
	// the footer of the next free block in the heap and update the block size
				sf_free_header* free_header = (sf_free_header*) current_header;
				remove_from_list(free_header);
				current_footer = GO_DOWN(current_header);
				current_header = BACK_TO_HEADER((void*) current_footer, or_header_size);
				update_header(current_header, 1,SHIFT(combined_size), padding);
				current_footer = GO_TO_FOOTER((void*)current_header, combined_size);
				update_footer(current_footer, 1, SHIFT(combined_size));

	// Now we must check whether tto shrink this new merged block or just return
	// this same block with extra allocation to avoid splintters
				if((combined_size - size) >= SMALLEST_BLOCK_SIZE){
					shrink(current_header, current_footer, combined_size, size, padding);
					return ptr;
				}
				else{
	// For this caes everything is already updated ad ready to return in case this
	// block doesn't need any shinking
					current_header = GO_UP(current_footer);
					if((combined_size - size) >= 16 && (void*)current_header < upper_bound && current_header->alloc == 0){
	// Right now your footer should be at the footer address of the new combined block
	// just go up to get the next header
						shrink(current_header, current_footer, combined_size, size, padding);
						return ptr;
						
					}

					internal -= (or_padding_size - padding);
					external -= (combined_size - or_header_size);
					return ptr;
				}
			}
			else
	// If all else fails then you just do a good all memcpy and return
				return expand(ptr, or_request_size);
		}
		else 
			return expand(ptr, or_request_size);
	}

	errno = EINVAL;
  	return NULL;
}

int sf_info(info* meminfo){

	meminfo->internal = internal;
	meminfo->external = external;
	meminfo->allocations = alllocations;
	meminfo->frees = frees;
	meminfo->coalesce = coalesces;

	return 0;
}
