#include "vm/virtualMemory.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include <string.h> 
#include <stdio.h> 

/* return if the frame is not null true */
static bool frame_valid (struct frame*);
static struct frame* frameTable_next_free (void);
static struct frame* frameTable_evict (void);
static struct frame* frameTable_next_evict (void);
static void frameTable_add_frame (struct frame*);

static struct frameTable ft;

/*
		This function create a new frame table, 
		and initialize all the elements of that
		table. Also initilize its lock
*/
void
frameTable_init (void)
{
	int i;
	for (i = 0; i < FT_SIZE; i++) {
		ft.table[i].busy = false;
		ft.table[i].kaddr = NULL;
	}	
	lock_init (&ft.ft_lock);
	lock_init (&ft.ft_evict_lock);
	lock_init (&ft.swap_lock);

	ft.cont = 0; 
	ft.swap_block = block_get_role (BLOCK_SWAP);
	ft.swap_bitmap = bitmap_create (block_size (ft.swap_block)/8);
	bitmap_set_all (ft.swap_bitmap, true);
}


/*
		This function find an empty slot in the frame table
		and point to page which the kernel allocate. After
		that its returns frame
*/
struct frame*
frameTable_alloc (void)
{
	struct frame* f;
	if (NULL == (f = frameTable_next_free ())) {
		frameTable_evict(); 
		f = frameTable_next_free ();
		frameTable_add_frame (f); 

	} else {
		ASSERT (frame_valid (f));
		frameTable_add_frame (f); 
	}
	return f;
}


/*
		This function free the page of the given frame,
		and initialize all the elements of the frame		
*/
void
frameTable_free (struct frame* f)
{
	ASSERT (frame_valid (f));	
	lock_acquire (&ft.ft_lock);
	
	f->uaddr = f->kaddr = f->page = NULL;
 	f->tid = 0; 
	f->busy = false;
	lock_release (&ft.ft_lock);
}

////////////////////////////////////////////////////////////////////
// FRAME PRIVATE FUNCTIONS																				//
////////////////////////////////////////////////////////////////////


/* return if the frame is not null true */
bool
frame_valid (struct frame* f) {
	return (f != NULL) ? true: false;
}


/* return if the frame is not null true */
struct frame*
frameTable_next_free (void)
{
	int i;
	for (i = 0; i < FT_SIZE; i++)
		if (ft.table[i].busy == false)
			return &ft.table[i];

	return NULL;
}


/* return if the frame is not null true */
struct frame*
frameTable_evict (void) 
{
	struct frame* f;
	struct thread* t;
	struct page* p;

	lock_acquire (&ft.ft_evict_lock);

	//Swap out the given frame
	if (NULL == (f = frameTable_next_evict ()))
		printf("No frames to be evict \n");

	t = tid_to_thread (f->tid);
	if (NULL == (p = pageTable_find (f->uaddr))) {
		p = pageTable_insert_light (f->uaddr);
	}

	p->frame = NULL;
	p->block = swap_out (p);
	p->status |= swapped;
	pagedir_clear_page (t->pagedir, p->uaddr);
	palloc_free_page (f->kaddr);

	//Reset the given element
	f->page = NULL;
	f->busy = false;
	f->tid = 0;
	f->kaddr = NULL;
	
	lock_release (&ft.ft_evict_lock);
	return f;
}


/* return if the frame is not null true */
struct frame*
frameTable_next_evict (void)
{
	int i,j;
	lock_acquire (&ft.ft_lock);
	for (j = 0; j < 2; j++) {
		for (i = 0; i < FT_SIZE; i++) {
			struct thread *t = tid_to_thread (ft.table[i].tid);

			if (!pagedir_is_accessed (t->pagedir, ft.table[i].uaddr)) {
				lock_release (&ft.ft_lock);
				return &ft.table[i];
			}else 
				pagedir_set_accessed (t->pagedir, ft.table[i].uaddr, false);
		}
	}
	lock_release (&ft.ft_lock);
	return NULL;
}


/* return if the frame is not null true */
void 
frameTable_add_frame (struct frame* f)
{
	if (NULL == (	f->kaddr = palloc_get_page (PAL_USER))) {
		frameTable_evict();
		if (NULL == (	f->kaddr = palloc_get_page (PAL_USER))) {
			printf("NO solution\n"); }
	
	}
	f->busy = true;
	f->tid = thread_current ()->tid;
}

/*Given a kernel address return the 
	frame which point to it */
struct frame*
frameTable_find_by_kaddr (uint8_t* kaddr) 
{
	int i;
	for (i = 0; i < FT_SIZE; i++)
		if (ft.table[i].kaddr == kaddr)
			return &ft.table[i];

	return NULL;
}

/*
		Given a page, this function load from the disk
		the swapped page, it fills the page structure in
		the for loop.
*/
void
swap_in (struct page* p)
{
	int i;
	p->frame = frameTable_alloc ();

	if (!pagedir_set_page (thread_current ()->pagedir, p->uaddr, p->frame->kaddr, true))
		printf ("ERROR Swapping in\n");

	for (i = 0; i < SECTOR_PAGE; i++)
		block_read 
		(ft.swap_block, (p->block * SECTOR_PAGE) + i, p->uaddr + (i * BLOCK_SECTOR_SIZE));

	bitmap_flip (ft.swap_bitmap, p->block);
	p->status = loaded;
}

/*
	Given a page, this function store a page from the
	main memory to the disk, it write in the given localization
	of the disk the page structure.
*/
size_t
swap_out (struct page* p)
{
	int i;
	p->block = bitmap_scan_and_flip (ft.swap_bitmap, 0, 1, true);

	if (p->uaddr == NULL || !is_user_vaddr (p->uaddr)) {
		printf("SWAPOUT UADDR \n");
		return 0;
	}

	if (p->block == BITMAP_ERROR) {
		printf("bitmap error: size: %i \n", bitmap_size (ft.swap_bitmap)); 
		return false;
	}

	for (i = 0; i < SECTOR_PAGE; i++)
		block_write 
		(ft.swap_block, (p->block * SECTOR_PAGE) + i, p->uaddr + (i * BLOCK_SECTOR_SIZE));
	ft.cont++;
	return p->block;
}

/* Just change the valuo of the given element */
void
swap_delete (size_t index) {
	bitmap_flip (ft.swap_bitmap, index);
}
