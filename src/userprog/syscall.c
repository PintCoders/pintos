#include "userprog/syscall.h"

//Given includes
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>

//Vicente's implementation
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "vm/virtualMemory.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/shutdown.h"
#include "devices/input.h"

/////////////////////////////////////////////////////////////////////////////
// DECLARING GLOBAL AND STATICS VARIABLES																	///
/////////////////////////////////////////////////////////////////////////////

typedef int pid_t;
typedef int mapid_t;
struct file_attr { 
	int fd;											/* 	File descriptor */
	int holder; 								/* 	tid_t of the holder thread */
	struct file *file;					/* 	File binded to this structure */
	struct list_elem elem;			/* 	element for the list */
};

struct list opened_files;			/*	list of opened files */
struct lock my_lock; 					/* 	lock required for synchronize */


/////////////////////////////////////////////////////////////////////////////
// DECLARING STATICS (PRIVATE) FUNCTIONS																	///
/////////////////////////////////////////////////////////////////////////////

static struct file_attr* fdtofile(int);


//This variables are needed for mmap
static int fd_by_mmap;
static void* last_mmap = NULL;
static struct page* last_page = NULL;

uint32_t* my_esp;

static inline int get_new_fd (void) 
{ 
	static int current_fd = 2;				/* 	last assignated fd	*/	
	return ++current_fd; 
}

static void syscall_handler (struct intr_frame *);
static void halt (void);
static pid_t exec (const char *);
static int wait (pid_t);
static bool create (const char *, unsigned);
static bool remove (const char *);
static int open (const char *);
static int filesize (int);
static int read (int, void *, unsigned);
static int write (int, const void *, unsigned);
static void seek (int, unsigned);
static unsigned tell (int);
static void close (int);
static mapid_t mmap (int, void*);
static void munmap (mapid_t);

/////////////////////////////////////////////////////////////////////////////
// IMPLEMENTATION OF FUNCTIONS																						///
/////////////////////////////////////////////////////////////////////////////

/* IS valid? */ 
inline bool
is_valid_usrptr (const void *usrptr)
{
	if ((usrptr != NULL) && (is_user_vaddr (usrptr) == true))
		return (pagedir_get_page(thread_current()->pagedir, usrptr) != NULL);
	else
		return false;
}

/*
	This function will return the file_attr for a given fd
	Will iterate over the list of files opened
*/
static struct file_attr*
fdtofile (int fd) 
{	
	struct list_elem* node = list_tail(&opened_files);
	for (; node != list_head(&opened_files); node = list_prev(node))
		if (list_entry(node, struct file_attr, elem)->fd == fd)
			return list_entry(node, struct file_attr, elem);

	return NULL;
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
	lock_init(&my_lock);
	list_init(&opened_files);
}

/* 
		ToDo
*/
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
		
	int32_t *esp = f->esp;
	my_esp = f->esp;
	uint32_t *eax = &(f->eax);

	if ( 	!is_valid_usrptr(esp) 		|| !is_valid_usrptr(esp + 1) ||
			 	!is_valid_usrptr(esp + 2) || !is_valid_usrptr(esp + 3))
		exit(-1);

	switch(*esp) {
		case SYS_HALT: 						halt 			(); 																					break;
		case SYS_EXIT: 						exit 			(*(esp + 1)); 																break;
		case SYS_EXEC:		*eax	=	exec 			((char*) *(esp + 1)); 												break;
		case SYS_WAIT:		*eax	=	wait 			(*(esp + 1)); 																break;
		case SYS_CREATE:	*eax	=	create		((char*) *(esp + 1), *(esp + 2)); 						break;
		case SYS_REMOVE:	*eax	=	remove 		((char*) *(esp + 1)); 												break;
		case SYS_OPEN: 		*eax	=	open			((char*) *(esp + 1)); 												break;
		case SYS_FILESIZE:*eax	=	filesize	(*(esp + 1)); 																break;
		case SYS_READ: 		*eax	=	read			(*(esp + 1), (void*) *(esp + 2), *(esp + 3)); break;
		case SYS_WRITE:		*eax	=	write			(*(esp + 1), (void*) *(esp + 2), *(esp + 3)); break;
		case SYS_SEEK: 						seek			(*(esp + 1), *(esp + 2)); 										break;
		case SYS_TELL: 		*eax	=	tell			(*(esp + 1)); 																break;
		case SYS_CLOSE: 					close 		(*(esp + 1)); 																break;
		case SYS_MMAP:		*eax	= mmap 			(*(esp + 1), (void*) *(esp+2));								break;
		case SYS_MUNMAP:  					munmap	  (*(esp + 1));																	break;
		default:																																					exit (-1);
	}
}

/*Simple*/
static void
halt (void) 
{
	shutdown_power_off();
}

void 
exit (int status)
{
	struct thread *current = thread_current ();
	if (current->mmf != NULL)
		munmap ((mapid_t)current->mmf);
	current->child_status = status;
	thread_exit ();
}

/*
		return the pid of the new process 
*/
static pid_t
exec (const char *file)
{
	if ( !is_valid_usrptr(file)) 
		exit(-1);

	return process_execute(file);
}

/*
		ToDo
*/
static int
wait (pid_t p UNUSED)
{
	return process_wait(p);			
}

static bool
create (const char *file, unsigned initial_size)
{
	if ( !is_valid_usrptr(file)) 
		exit(-1);

	lock_acquire(&my_lock);
		bool out = filesys_create( file, initial_size);	
	lock_release(&my_lock);
	return out;
}

static bool
remove (const char *file)
{
	if (!is_valid_usrptr(file)) 
		exit(-1);

	lock_acquire(&my_lock);
		bool out = filesys_remove(file);
	lock_release(&my_lock);
	return out;
}

static int
open (const char *file)
{
	if (!is_valid_usrptr(file))
		exit(-1);

	struct file_attr *f;
	lock_acquire(&my_lock);

	f = calloc(1, sizeof(struct file_attr));
	f->file = filesys_open(file);

	if (f->file == NULL)
		goto exit;

	f->fd = get_new_fd();
	f->holder = thread_current()->tid;
	list_push_back(&opened_files, &f->elem);

	lock_release(&my_lock);

	return f->fd;

exit:
	free(f);
	lock_release(&my_lock);
	return -1;
}

static int
filesize (int fd)
{
	struct file_attr *f;
	if (NULL == (f = fdtofile(fd)))
		return -1;
	else 
		return file_length(f->file);
}

static int 
read (int fd, void *buffer, unsigned length)
{
	struct file_attr *f;
	unsigned i = 0;
	unsigned buf_s = length;
	off_t bytes_readed;
	struct thread *t = thread_current();
	struct frame *fr;
	struct page* pg;
	void* buf;

	//It will allocate all the pages needed to read the file
	for (buf = buffer; buf != NULL;) {
		if (!is_user_vaddr(buf))
			exit(-1);

		pg = pageTable_find (pg_round_down (buf));
		if (pagedir_get_page (t->pagedir, buf) == NULL) {
			if ((buf >= (my_esp - 32)) && pg == NULL)
				if (NULL != (fr = frameTable_alloc ()))
					pagedir_set_page (t->pagedir, pg_round_down (buf), fr->kaddr, true);	
				else
					exit(-1);
			else 
				exit (-1);
		}
		if (buf_s == 0)
			buf = NULL;

		else if (buf_s > PGSIZE) {
			buf += PGSIZE;
			buf_s -= PGSIZE;
		} else {
			buf = buffer + length- 1;
			buf_s = 0;
		}
	}

	lock_acquire (&my_lock);
	switch (fd) {
		case STDIN_FILENO:	
			for (	i = 0; i < length; i++) 
				((uint8_t*) buffer) [i] = input_getc();

			lock_release (&my_lock);
			return (int)length;

		case STDOUT_FILENO:
			lock_release (&my_lock);
			return -1;

		default:
			if (NULL == (f = fdtofile(fd))) {
				lock_release (&my_lock);
				return -1;

			} else {
				bytes_readed = file_read (f->file, buffer, length);
				lock_release (&my_lock);
			}
			return bytes_readed;
	}
}

	static int
write (int fd, const void *buffer, unsigned length)
{
	struct file_attr *f;
	struct thread *t;
	struct page *p;
	off_t bytes_writted;

	if (!is_valid_usrptr(buffer) || !is_valid_usrptr(buffer+length))
		exit(-1);

	t = thread_current();
	if (t->mmf != NULL && fd > 2){
		p = pageTable_find (t->mmf);
		p->writted = false;
	}
	lock_acquire(&my_lock);
	switch (fd) {
		case STDOUT_FILENO:	
			putbuf(buffer, length);
			lock_release(&my_lock);
			return length;

		case STDIN_FILENO:
			lock_release(&my_lock);
			return -1;

		default:
			if ( NULL == (f = fdtofile(fd))) {
				lock_release(&my_lock);
				return -1;

			} else {
				bytes_writted = file_write(f->file, buffer, length);
				lock_release(&my_lock);
				return bytes_writted;
			}
	}
}

	static void
seek (int fd, unsigned position)
{
	struct file_attr *f;
	if (NULL == (f = fdtofile(fd)))
		exit(-1);

	lock_acquire(&my_lock);
	file_seek(f->file, position);
	lock_release(&my_lock);
}

	static unsigned
tell (int fd)
{
	struct file_attr *f;
	if (NULL == (f = fdtofile(fd)))
		exit(-1);

	return file_tell(f->file);
}

	static void
close (int fd)
{
	if (fd != STDIN_FILENO && fd != STDOUT_FILENO) {
		if (last_mmap != NULL && fd == fd_by_mmap) {
			//struct page *pg = pageTable_find (last_mmap);
			//last_page->writted = true;
			//munmap((mapid_t)last_mmap);
			struct thread* t = thread_current();
			*(uint32_t*)last_mmap = 123123;
			pagedir_clear_page (t->pagedir, last_mmap);
		}
		struct file_attr *f;
		if ( NULL != (f = fdtofile(fd))) {
			lock_acquire(&my_lock);
			file_close(f->file);
			list_remove(&f->elem);
			free(f);
			lock_release(&my_lock);
		}
	}
}

/*
		This function will create a new mmap creating 
		the pages needed to read all the information in 
		the file in case that the file is not empty
		If we need to access to that information it will
		be load by the lazy load algorithm in the page 
		fault
*/
static mapid_t
mmap (int fd, void *addr)
{
	struct file_attr *f;
	int length;
	int ofs = 0;
	struct thread *t;
	struct page *pg;

	//if the addr is not proper
	if (addr == NULL || addr == 0x0 || fd == 0 || fd == 1)
		return -1;
	
	if (addr != pg_round_down (addr) || is_valid_usrptr(addr))
		return -1;
		
	f = fdtofile (fd);
	fd_by_mmap = fd;
	last_mmap = addr;

	if (f == NULL)
		return -1;

	length = file_length (f->file);
	if (length == 0)
		return -1;
	
	t = thread_current ();
	t->mmf = addr;

	//Create the empty pages for the later lazy loading
	while (ofs < length)	{
		f->file = file_reopen (f->file);
		pg = pageTable_insert_mmf (addr + ofs, f->file, ofs, true);

		if(!pg) 
			return -1;
		pg->file = f->file;
		pg->ofs = ofs;
		if (length - ofs > PGSIZE)	{
			pg->read_bytes = PGSIZE;
			pg->zero_bytes = 0;
		} else {
			pg->read_bytes = length - ofs;
			pg->zero_bytes = PGSIZE - pg->read_bytes;
		}
		ofs += PGSIZE;
	}
	return (mapid_t) addr;
}

/*
 	Given an address this function will write to 
	the disk all the information and later reset 
	and delete the pages which contain that information
*/
static void 
munmap (mapid_t mapid)
{
	void *addr = (void*) mapid;
	struct page *pg;
	struct thread *t;

	t = thread_current();
	pg = pageTable_find (addr);
	pg->file = file_reopen (pg->file);

	//If we have to write to the disk
	if (pg->writted == true){
		file_seek (pg->file, pg->ofs);
		file_write (pg->file, pg->uaddr, pg->read_bytes);

		addr += PGSIZE;
		pageTable_delete (pg);
	}
		
	//reset all the values
	t->mmf = NULL;
	file_close(pg->file);
	pg->status |= loaded;
	pagedir_clear_page (t->pagedir, pg->uaddr);
	fd_by_mmap = 0;
	last_mmap = (void*)mapid;
}
