#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/inode.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/synch.h"
#include "devices/shutdown.h"
#include "threads/palloc.h"
#include "userprog/process.h"
#include "devices/input.h"
#include "threads/malloc.h"
#include "filesys/directory.h"
#include "filesys/free-map.h"

typedef pid_t;

static void syscall_handler(struct intr_frame *);

static void is_valid_user(const void *);

static int *get_arg(int *, int, bool);
struct file *get_file_fd(int);
int create_fd(char *);
void close_fd(int);
void close_all_fd(void);

bool chdir(const char *);
bool mkdir(const char *);
bool readdir(int, char *);
bool isdir(int);
int inumber(int);
char *path_lookup(const char *, bool, struct inode **, struct inode **);
void exit(int);
void halt(void);
int write(int fd, const void *, unsigned);
pid_t exec(const char *);
bool create(const char *, unsigned);
bool remove(const char *);
int filesize(int);
void seek(int, unsigned);
unsigned tell(int);
int read(int, void *, unsigned);

struct lock file_lock;

void syscall_init(void)
{
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");

  lock_init(&file_lock);
}

//Tarun is driving
static void
syscall_handler(struct intr_frame *f)
{
  int *my_esp = (int *)f->esp;
  int fd = 0;
  switch ((int)*get_arg(my_esp, 0, false))
  {

  case SYS_HALT:
    halt();
    break;

  case SYS_EXIT:
    exit((int)*get_arg(my_esp, 1, false));
    break;

  case SYS_EXEC:
    f->eax = exec((char *)*get_arg(my_esp, 1, true));
    break;

  case SYS_WAIT:
    f->eax = process_wait((int)*get_arg(my_esp, 1, false));
    break;

  //Aaron is driving
  case SYS_CREATE:
    f->eax = create((char *)*get_arg(my_esp, 1, true),
                    (unsigned)*get_arg(my_esp, 2, false));
    break;

  case SYS_REMOVE:
    f->eax = remove((char *)*get_arg(my_esp, 1, true));
    break;

  case SYS_OPEN:
    fd = create_fd((char *)*get_arg(my_esp, 1, true));
    f->eax = fd;
    break;

  case SYS_FILESIZE:
    f->eax = filesize((int)*get_arg(my_esp, 1, false));
    break;
  //Chris is driving
  case SYS_READ:
    f->eax = read((int)*get_arg(my_esp, 1, false),
                  (void *)*get_arg(my_esp, 2, true),
                  (unsigned)*get_arg(my_esp, 3, false));
    break;

  case SYS_WRITE:
    f->eax = write((int)*get_arg(my_esp, 1, false),
                   (void *)*get_arg(my_esp, 2, true),
                   (unsigned)*get_arg(my_esp, 3, false));
    break;

  case SYS_SEEK:
    seek((int)*get_arg(my_esp, 1, false),
         (unsigned)*get_arg(my_esp, 2, false));
    break;

  case SYS_TELL:
    f->eax = tell((int)*get_arg(my_esp, 1, false));
    break;

  case SYS_CLOSE:
    close_fd((int)*get_arg(my_esp, 1, false));
    break;
  case SYS_CHDIR:
    f->eax = chdir((const char *)*get_arg(my_esp, 1, true));
    break;
  case SYS_MKDIR:
    f->eax = mkdir((const char *)*get_arg(my_esp, 1, true));
    break;
  case SYS_READDIR:
    f->eax = readdir((int)*get_arg(my_esp, 1, false),
                     (const char)*get_arg(my_esp, 2, true));
    break;
  case SYS_ISDIR:
    f->eax = isdir((int)*get_arg(my_esp, 1, false));
    break;
  case SYS_INUMBER:
    f->eax = inumber((int)*get_arg(my_esp, 1, false));
    break;
  }
}

bool chdir(const char *dir)
{
  lock_acquire(&file_lock);

  if (strlen(dir) == 0)
  {
    lock_release(&file_lock);
    return false;
  }

  struct inode *parent;
  struct inode *child;
  char *ret;
  ret = path_lookup(dir, true, &parent, &child);

  if (ret == NULL)
  {
    lock_release(&file_lock);
    return false;
  }
  else
  {
    thread_current()->cwd = dir_open(child);
    lock_release(&file_lock);
    return true;
  }

  return false;
}

bool mkdir(const char *dir)
{
  lock_acquire(&file_lock);

  if (strlen(dir) == 0)
  {
    lock_release(&file_lock);
    return false;
  }

  struct inode *parent_inode;
  char *ret;
  ret = path_lookup(dir, false, &parent_inode, NULL);

  if (ret == NULL)
  {
    //  printf("makedir 1\n");
    lock_release(&file_lock);
    return false;
  }
  else
  {
    // printf("makedir 2\n");
    block_sector_t sector;
    bool check = free_map_allocate(1, &sector);
    if (!check)
      PANIC("Should not be here.");

    struct inode *my_inode;
    dir_create(sector, 2);
    my_inode = inode_open(sector);
    dir_add(dir_open(parent_inode), ret, sector);
    lock_release(&file_lock);
    return true;
  }
}

bool readdir(int fd, char *name)
{
  return false;
}

bool isdir(int fd)
{
  struct file *file = get_file_fd(fd);
  return file->inode->is_directory;
}

int inumber(int fd)
{
  return 0;
}

/* Used to look up directories from an absolute or relative path */
char *path_lookup(const char *dir, bool must_exist, struct inode **parent,
  struct inode **child)
{
  bool absolute = dir[0] == '/';
  struct dir *current_directory = absolute ? dir_open_root() 
    : thread_current()->cwd;

  struct inode *parent_inode = current_directory->inode;
  struct inode *current_inode = NULL;

  char *saveptr;
  char *token = NULL;
  char *next_token = NULL;

  token = strtok_r(dir, "/", &saveptr);
  //while (((token = strtok_r(saveptr, "/", &saveptr)) != NULL))
  while (token != NULL)
  {
    next_token = strtok_r(NULL, "/", &saveptr);
    if (next_token == NULL)
    {
      if (!must_exist && dir_lookup(current_directory, token, &current_inode))
      {
        return NULL;
      }
      else
      {
        if(parent != NULL)
          *parent = parent_inode;
        if(child != NULL)
          *child = current_inode;
        return token;
      }
    }
    else if (!dir_lookup(current_directory, token, &current_inode))
      return NULL;

    parent_inode = current_inode;
    current_directory = dir_open(current_inode);
    token = next_token;
  }
  return NULL;
}

//Exit thread with status
//Aaron is driving
void exit(int status)
{
  struct thread *t = thread_current();
  struct list *child_list = &t->parent->child_list;

  //Update parent's child_info with child's exit status
  struct list_elem *e;
  for (e = list_begin(child_list); e != list_end(child_list);
       e = list_next(e))
  {
    struct child_info *entry = list_entry(e, struct child_info, elem);
    if (entry->tid == t->tid)
    {
      entry->exited = true;
      entry->exit_code = status;
      break;
    }
  }

  //Clear the exiting thread's child list
  //Henry is driving
  child_list = &t->child_list;
  while (!list_empty(child_list))
  {
    struct child_info *entry = list_entry(list_pop_back(child_list),
                                          struct child_info, elem);
    free(entry);
  }

  char buf[100];
  int size = snprintf(buf, 100, "%s: exit(%d)\n", t->name, status);
  write(1, buf, size);

  if (t->parent->waiting_on == t->tid)
  {
    sema_up(&thread_current()->parent->child_wait);
  }

  thread_exit();
}

//Shutdown the system
//Henry is driving
void halt(void)
{
  shutdown_power_off();
}

//Output size bytes from buffer to specified file
//Chris is driving
int write(int fd, const void *buffer, unsigned size)
{
  //Write to console
  if (fd == 1)
  {
    putbuf(buffer, size);
    return size;
  }
  //Write to file
  else
  {
    struct file *file = get_file_fd(fd);
    lock_acquire(&file_lock);
    int written = file_write(file, buffer, size);
    lock_release(&file_lock);
    return written;
  }
}

//Execute specified program in a new thread
//Tarun is driving
pid_t exec(const char *cmd)
{
  tid_t id = process_execute(cmd);

  //Check if child failed to initialize
  if (thread_current()->child_code == -1)
    return -1;

  return (pid_t)id;
}

//Create a file
//Chris is driving
bool create(const char *file, unsigned initial_size)
{
  lock_acquire(&file_lock);
  bool ret = filesys_create(file, initial_size);
  lock_release(&file_lock);
  return ret;
}

//Remove a file
//Aaron is driving
bool remove(const char *file)
{
  lock_acquire(&file_lock);
  bool ret = filesys_remove(file);
  lock_release(&file_lock);
  return ret;
}

//Return file's size
//Aaron is driving
int filesize(int fd)
{
  lock_acquire(&file_lock);
  struct file *file = get_file_fd(fd);
  int length = -1;
  if (file != NULL)
    length = file_length(file);
  lock_release(&file_lock);
  return length;
}

//Changes next byte to be read or written
//Henry is driving
void seek(int fd, unsigned position)
{
  lock_acquire(&file_lock);
  struct file *file = get_file_fd(fd);
  file_seek(file, position);
  lock_release(&file_lock);
}

//Returns next byte to be read or written
//Henry is driving
unsigned
tell(int fd)
{
  lock_acquire(&file_lock);
  struct file *file = get_file_fd(fd);
  unsigned ret = file_tell(file);
  lock_release(&file_lock);
  return ret;
}

//Read size bytes from file into buffer
//Chris is driving
int read(int fd, void *buffer, unsigned size)
{
  int ret = 0;

  //Get keyboard input
  if (fd == 0)
  {
    unsigned i;
    for (i = 0; i < size; i++)
    {
      *((char *)buffer) = input_getc();
      buffer = (char *)buffer + 1;
    }
    ret = size;
  }
  //Read from file
  else
  {
    struct file *file = get_file_fd(fd);
    lock_acquire(&file_lock);
    ret = file_read(file, buffer, size);
    lock_release(&file_lock);
  }
  return ret;
}

//Returns file struct from file descriptor
//Aaron is driving
struct file *
get_file_fd(int fd)
{
  struct list *file_list = &thread_current()->file_list;
  if (!list_empty(file_list))
  {
    struct list_elem *e;
    for (e = list_begin(file_list); e != list_end(file_list);
         e = list_next(e))
    {
      struct fd_file *entry = list_entry(e, struct fd_file, elem);
      if (entry->fd == fd)
      {
        return entry->file;
      }
    }
  }
  //if not found, exit with error
  exit(-1);
}

//Open specified file and generate a file descriptor
//Tarun is driving
int create_fd(char *file_name)
{
  lock_acquire(&file_lock);
  // differentiate between directory and file

  struct file *file_obj = filesys_open(file_name);

  lock_release(&file_lock);
  if (file_obj == NULL)
  {
    return -1;
  }

  //Get the current highest fd and increment that by 1 to get the new one
  struct list *file_list = &thread_current()->file_list;
  int fd = 2;
  if (!list_empty(file_list))
  {
    fd = list_entry(list_back(file_list), struct fd_file, elem)->fd + 1;
  }

  //Store fd_file in the thread's file list
  struct fd_file *new_entry = malloc(sizeof(struct fd_file));
  new_entry->fd = fd;
  new_entry->file = file_obj;

  list_push_back(file_list, &new_entry->elem);
  return fd;
}

//Close a file and it's corresponding file descriptor
//Henry is driving
void close_fd(int fd)
{
  struct list *file_list = &thread_current()->file_list;
  if (!list_empty(file_list))
  {
    struct list_elem *e;
    for (e = list_begin(file_list); e != list_end(file_list);
         e = list_next(e))
    {
      struct fd_file *entry = list_entry(e, struct fd_file, elem);
      if (entry->fd == fd)
      {
        lock_acquire(&file_lock);
        file_close(entry->file);
        lock_release(&file_lock);

        list_remove(e);
        free(entry);
        break;
      }
    }
  }
}

//Close all files and file descriptors for the current thread
//Chris is driving
void close_all_fd()
{
  struct list *file_list = &thread_current()->file_list;
  struct list_elem *e;
  while (!list_empty(file_list))
  {
    e = list_pop_front(file_list);
    struct fd_file *entry = list_entry(e, struct fd_file, elem);
    lock_acquire(&file_lock);
    file_close(entry->file);
    lock_release(&file_lock);

    free(entry);
  }
}

//Check if the provided pointer is a valid user address
//Tarun is driving
static void
is_valid_user(const void *vaddr)
{

  struct thread *t = thread_current();
  // failed one of the three conditions
  if ((vaddr == NULL) || (!is_user_vaddr(vaddr)) ||
      (pagedir_get_page(t->pagedir, vaddr) == NULL))
  {
    exit(-1);
  }
}

/*
Assuming a system call's arguments are on the stack, get the nth argument
of the system call given esp and make sure it is a valid address.

If validate is true, the address that the argument contains is also
validated.
*/
//Aaron is driving
static int *
get_arg(int *esp, int arg, bool validate)
{
  is_valid_user(esp + arg);

  if (validate)
    is_valid_user((void *)*(esp + arg));

  return esp + arg;
}
