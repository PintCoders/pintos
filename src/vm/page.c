#include "vm/virtualMemory.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <stdio.h>

/* Create the frame Table */
void
virtualMemory_init (void) {
	frameTable_init ();
}

/*
 		It is needed since we will create a new page 
		without allocating a memory for the page
*/
struct page* 
pageTable_insert_light (void *addr)
{
	struct page *p;
	struct thread* t = thread_current();

	p = malloc (sizeof (struct page));
	
	p->type = normal;
	p->status = 0;
	p->uaddr = addr;
	p->block = 0;
	p->frame = NULL;

	hash_insert (&t->pageTable, &p->h_elem);

	return p;
}

/* 
	Create a new page and store it in the hash table (pagetable)
	of the current thread, it will be use for stack growth
	Also it allocate a frame.
*/
struct page* 
pageTable_insert (void *addr)
{
	struct page *p;
	struct thread* t = thread_current();

	p = malloc (sizeof (struct page));
	
	p->type = normal;
	p->status = 0;
	p->uaddr = addr;
	p->block = 0;
	p->frame = frameTable_alloc ();

	hash_insert (&t->pageTable, &p->h_elem);

	return p;
}

/*
	This function create a new entry in the hash table of file type
	is needed to be lazy loadder later in page fault
	Vicente
*/
struct page* 
pageTable_insert_file (void *uaddr, struct file *file,
											off_t ofs, uint32_t read_bytes,
											uint32_t zero_bytes, bool writable) {
	struct page *p;
	struct thread* t = thread_current();
	p = malloc (sizeof (struct page));

	p->type = file_page;
	p->status = 0;
	p->uaddr = uaddr;
	p->block = 0;

	p->file = file;
	p->ofs  = ofs;
	p->read_bytes = read_bytes;
	p->zero_bytes = zero_bytes;
	p->writable = writable;
	hash_insert (&t->pageTable, &p->h_elem);

	return p;
}

/*
	This function create a new entry in the hash table of mmapfile type
	is needed to be lazy loadder later in page fault
	Vicente
*/
struct page* 
pageTable_insert_mmf (void *uaddr, struct file *file,
											off_t ofs, bool writable) {
	struct page *p;
	struct thread* t = thread_current();
	p = malloc (sizeof (struct page));

	if(pageTable_find (uaddr) != NULL)
		return NULL;

	p->type = mmf_page;
	p->status = 0;
	p->writted = false;
	p->uaddr = uaddr;
	p->block = 0;

	p->file = file;
	p->ofs  = ofs;
	p->read_bytes = 0 ;
	p->zero_bytes = 0;
	p->writable = writable;
	hash_insert (&t->pageTable, &p->h_elem);

	return p;
}

/* Delete the given page */
void
pageTable_delete (struct page *p)
{
	struct thread* t = thread_current();
 	if (p->frame == NULL)	
		printf("PAGE TABLE DELETE ERROR\n");
	frameTable_free (p->frame);
	hash_delete (&t->pageTable, &p->h_elem); 
}

/* Given a user address return the page 
		which contain this address */
struct page* 
pageTable_find (void* uaddr)
{
	struct hash_elem* he;
	struct page p;
	struct thread* t = thread_current();
	
	p.uaddr = uaddr;
	he = hash_find (&t->pageTable, &p.h_elem);
	
	return (he == NULL) ? NULL: hash_entry (he, struct page, h_elem);	
}



////////////////////////////////////////////////////////////////////
// HASH FUNCTIONS																									//
////////////////////////////////////////////////////////////////////

/* Needed for handle the key */
unsigned
page_hash (const struct hash_elem *he, void *aux UNUSED)
{
	const struct page *pg;
	pg = hash_entry (he, struct page, h_elem);
	return hash_bytes (&pg->uaddr, sizeof (pg->uaddr));
}

/* Needed for sort elements* */
bool
comp_va_page (const struct hash_elem *hea, const struct hash_elem *heb,
	 void *aux UNUSED)
{
	const struct page *pga;
	const struct page *pgb;

	pga = hash_entry (hea, struct page, h_elem);
	pgb = hash_entry (heb, struct page, h_elem);
	
	return pga->uaddr < pgb->uaddr;
}

/* In case that the page is swapped, delete that slot*/
void
page_hash_delete (struct hash_elem* hea, void* aux UNUSED) 
{
	struct page* p = hash_entry (hea, struct page, h_elem);
	if (p->status & swapped)
		swap_delete (p->block);		

	free (p);
}
