#ifndef _VIRTUAL_MEMORY_
#define _VIRTUAL_MEMORY_

#include <hash.h>
#include <list.h>
#include <kernel/bitmap.h>
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/block.h"
#include "filesys/off_t.h"

#define FT_SIZE 380
#define SECTOR_PAGE PGSIZE/BLOCK_SECTOR_SIZE

////////////////////////////////////////////////////////////////////
// Function regarding virtual memory															//
// Create the frame table and pagedir table												//
////////////////////////////////////////////////////////////////////
void virtualMemory_init (void); 

////////////////////////////////////////////////////////////////////
// ADT: FRAME TABLE																								//
// DATA STRUCTURES: frameTable (array of frames)									//
// AUTHOR: Vicente Adolfo Bolea Sanchez (vicente.bolea@gmail.com)	//
//																																//
// PRIVATE FUNCTIONS:																							//
//		static bool frame_valid (struct frame*);										//
//																																//
// HOW TO USE: 																										//
//	struct frameTable f;																					//
//	struct frame* my_frame;																				//
//																																//
//	frameTable_init ();																						//
//	my_frame = frameTable_alloc ();																//
//	frameTable_free (my_frame);																		//
//																																//
////////////////////////////////////////////////////////////////////


struct frame {
	bool busy;
	int tid;
	void* kaddr;
	void* uaddr;
	uint32_t* page;
};

struct frameTable {
	struct lock ft_lock;
	struct lock ft_evict_lock;
	struct lock swap_lock;

	struct frame table [FT_SIZE];
	struct bitmap* swap_bitmap;
	struct block* swap_block;
	int cont;
};

void frameTable_init (void); 						//constructor
struct frame* frameTable_alloc (void);  
void frameTable_free (struct frame*);  
struct frame* frameTable_find_by_kaddr (uint8_t*);

struct page;
//Functions regarding swapping
void swap_in (struct page*);
size_t swap_out (struct page*);
void swap_delete (size_t);


////////////////////////////////////////////////////////////////////
// ADT: SUPLEMENTAL PAGE TABLE																		//
// DATA STRUCTURES: hash table of pages	 (key = virtual addr)			//
//																																//
// AUTHOR: 			Vicente Adolfo Bolea Sanchez 											//
// EMAIL:				vicente.bolea@gmail.com														//
// STUDENT ID: 	20122901																					//
//																																//
// AUTHOR: 			Choi Woohyuk 																			//
// EMAIL:																													//
// STUDENT ID:																										//
//																																//
// HOW TO USE: 																										//
////////////////////////////////////////////////////////////////////


enum page_type {normal = 1, file_page = 2, mmf_page = 3};
enum page_status {loaded = 2, swapped = 2};

struct page {
	enum page_type type;
	enum page_status status;
	
	bool writted;
	struct frame* frame;
	void* uaddr;
	size_t block;

	struct file *file;
	off_t ofs;
	uint32_t read_bytes;
	uint32_t zero_bytes;
	bool writable;

	struct hash_elem h_elem;
};

struct page* pageTable_insert (void*);
struct page* pageTable_insert_light (void*);
struct page* pageTable_insert_file (void*,
	 struct file*, off_t, uint32_t, uint32_t, bool);
struct page* pageTable_insert_mmf (void *, struct file *, off_t, bool);

void pageTable_delete (struct page*);
struct page* pageTable_find (void*);

////////////////////////////////////////////////////////////////////
// HASH FUNCTIONS										 															//
////////////////////////////////////////////////////////////////////

unsigned page_hash (const struct hash_elem*, void*);
bool comp_va_page (const struct hash_elem*, const struct hash_elem*, void*);
void page_hash_delete (struct hash_elem*, void*);

#endif
