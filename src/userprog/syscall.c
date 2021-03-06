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
struct fd_file *get_file_fd(int);
int create_fd(char *);
void close_fd(int);
void close_all_fd(void);

bool chdir(const char *);
bool mkdir(const char *);
bool readdir(int, char *);
bool isdir(int);
int inumber(int);
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

void syscall_init(void)
{
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
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
  //Henry is driving
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
                     (const char *)*get_arg(my_esp, 2, true));
    break;
  case SYS_ISDIR:
    f->eax = isdir((int)*get_arg(my_esp, 1, false));
    break;
  case SYS_INUMBER:
    f->eax = inumber((int)*get_arg(my_esp, 1, false));
    break;
  }
}

//Change cwd to new directory dir, based on
//relative or absolute path
bool chdir(const char *dir)
{
  char *dir_copy = malloc(strlen(dir) + 1);
  strlcpy(dir_copy, dir, strlen(dir) + 1);

  bool ret;
  ret = filesys_chdir(dir_copy);

  free(dir_copy);
  return ret;
}

//Makes new directory dir, based on relative or
//absolute path
bool mkdir(const char *dir)
{
  char *dir_copy = malloc(strlen(dir) + 1);
  strlcpy(dir_copy, dir, strlen(dir) + 1);

  bool ret;
  ret = filesys_mkdir(dir_copy);

  free(dir_copy);
  return ret;
}

//Chris is driving
//Reads directory entry from specified fd entry,
//and can be absolute or relative path
bool readdir(int fd, char *name)
{
  struct fd_file *fd_file = get_file_fd(fd);
  return dir_readdir(fd_file->dir, name);
}

//Returns true if fd is directory, and false if not
bool isdir(int fd)
{
  struct fd_file *fd_file = get_file_fd(fd);
  return fd_file->dir != NULL;
}

//Returns inode number, a.k.a the underlying sector
//number of the specified fd
int inumber(int fd)
{
  struct fd_file *fd_file = get_file_fd(fd);
  if (fd_file->file != NULL)
    return inode_get_inumber(fd_file->file->inode);
  else
    return inode_get_inumber(fd_file->dir->inode);
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

  //Close the current working directory of exiting thread
  dir_close(t->cwd);

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
    struct fd_file *fd_file = get_file_fd(fd);
    //Make sure the fd is a file to write to specified file
    if (fd_file->dir != NULL)
      return -1;
    int written = file_write(fd_file->file, buffer, size);
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
  char *file_copy = malloc(strlen(file) + 1);
  strlcpy(file_copy, file, strlen(file) + 1);

  bool ret = filesys_create(file_copy, initial_size);

  free(file_copy);
  return ret;
}

//Remove a file
//Aaron is driving
bool remove(const char *path)
{
  bool ret;

  //Make copies of the path, incase it gets changed
  char *first_path_copy = malloc(strlen(path) + 1);
  strlcpy(first_path_copy, path, strlen(path) + 1);

  char *second_path_copy = malloc(strlen(path) + 1);
  strlcpy(second_path_copy, path, strlen(path) + 1);

  //Use rmdir or remove method, depending on if
  //removing file or directory
  bool isdir = filesys_isdir(first_path_copy);
  if (isdir)
    ret = filesys_rmdir(second_path_copy);
  else
    ret = filesys_remove(second_path_copy);

  free(first_path_copy);
  free(second_path_copy);
  return ret;
}

//Return file's size
//Aaron is driving
int filesize(int fd)
{
  struct file *file = get_file_fd(fd)->file;
  int length = -1;
  if (file != NULL)
    length = file_length(file);
  return length;
}

//Changes next byte to be read or written
//Henry is driving
void seek(int fd, unsigned position)
{
  struct file *file = get_file_fd(fd)->file;
  file_seek(file, position);
}

//Returns next byte to be read or written
//Henry is driving
unsigned
tell(int fd)
{
  struct file *file = get_file_fd(fd)->file;
  unsigned ret = file_tell(file);
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
    struct file *file = get_file_fd(fd)->file;
    ret = file_read(file, buffer, size);
  }
  return ret;
}

//Returns file struct from file descriptor
//Aaron is driving
struct fd_file *
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
        return entry;
      }
    }
  }
  //if not found, exit with error
  exit(-1);
}

//Open specified file and generate a file descriptor
//Tarun is driving
int create_fd(char *path)
{
  char *first_path_copy = malloc(strlen(path) + 1);
  strlcpy(first_path_copy, path, strlen(path) + 1);

  char *second_path_copy = malloc(strlen(path) + 1);
  strlcpy(second_path_copy, path, strlen(path) + 1);

  struct file *file_obj;
  struct dir *dir_obj;

  //Attempt to open as a file and directory, and distinguish
  //which one it is to set it to proper field in fd_file
  file_obj = filesys_open(first_path_copy);
  dir_obj = filesys_opendir(second_path_copy);

  bool isdir = dir_obj != NULL;

  //Ensure that directory or file was created
  if ((isdir && dir_obj == NULL) || (!isdir && file_obj == NULL))
  {
    free(first_path_copy);
    free(second_path_copy);
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
  if (isdir)
  {
    new_entry->dir = dir_obj;
    new_entry->file = NULL;
  }
  else
  {
    new_entry->file = file_obj;
    new_entry->dir = NULL;
  }

  list_push_back(file_list, &new_entry->elem);

  free(first_path_copy);
  free(second_path_copy);
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
        if (entry->file != NULL)
          file_close(entry->file);
        else
          dir_close(entry->dir);

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
    if (entry->file != NULL)
      file_close(entry->file);
    else
      dir_close(entry->dir);

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
