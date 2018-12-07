#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);
bool path_lookup(const char *, bool, char **, struct inode **, 
  struct inode **);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *path, off_t initial_size) 
{
  char *name;
  struct inode *parent_inode;

  if(!path_lookup(path, false, &name, &parent_inode, NULL))
    return false;

  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open(parent_inode);
  inode_acquire_dirlock(dir->inode);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  inode_release_dirlock(dir->inode);
  dir_close (dir);
  return success; 
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *path)
{
  char *name; 
  struct inode *child_inode;

  if(!path_lookup(path, true, &name, NULL, &child_inode))
    return NULL;
  
  if(inode_isdir(child_inode)){
    inode_close(child_inode);
    return NULL;
  }

  return file_open (child_inode);
}

struct dir *
filesys_opendir (const char *path)
{
  char *name; 
  struct inode *child_inode;
  if(!path_lookup(path, true, &name, NULL, &child_inode))
    return NULL;
  if(!inode_isdir(child_inode)){
    inode_close(child_inode);
    return NULL;
  }
  return dir_open (child_inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *path) 
{
  char *name;
  struct inode *parent_inode;
  if(!path_lookup(path, true, &name, &parent_inode, NULL))
    return false;

  struct dir *dir = dir_open(parent_inode);
  
  inode_acquire_dirlock(dir->inode);
  bool success = dir != NULL && dir_remove (dir, name);
  inode_release_dirlock(dir->inode);
  
  dir_close (dir);
  return success;
}

bool
filesys_mkdir(const char *path)
{
  struct inode *parent_inode;
  char *name;
  
  if (!path_lookup(path, false, &name, &parent_inode, NULL))
    return false;

  block_sector_t sector;
  
  if(!free_map_allocate(1, &sector))
    return false;

  struct dir *parent_directory;
  struct dir *new_directory;

  dir_create(sector, 2);
  parent_directory = dir_open(parent_inode);

  new_directory = dir_open(inode_open(sector));
  dir_add(new_directory, ".", sector);
  dir_add(new_directory, "..", inode_get_inumber(parent_directory->inode));
  dir_close(new_directory);

  dir_add(parent_directory, name, sector);
  dir_close(parent_directory);

  return true;
}

bool
filesys_chdir(const char *path)
{
  struct inode *child;

  if(!path_lookup(path, true, NULL, NULL, &child))
    return false; 

  //close old cwd
  dir_close(thread_current()->cwd);

  thread_current()->cwd = dir_open(child);
  return true;
}

bool filesys_rmdir(const char *path)
{
  struct inode *child;
  struct inode *parent;
  char *dir_name;

  if(!path_lookup(path, true, &dir_name, &parent, &child))
    return false;

  if(!inode_isdir(child)){
    inode_close(parent);
    inode_close(child);
    return false;
  }
  
  struct dir *dir;
  dir = dir_open(child);

  if(inode_get_inumber(dir->inode) == 
    inode_get_inumber(thread_current()->cwd->inode)){
      dir_close(dir);
      return false;
    }
  
  if(inode_isopen(dir->inode)){
    dir_close(dir);
    return false;
  }

  char name[NAME_MAX + 1]; 
  if(dir_readdir(dir, name)){
    dir_close(dir);
    return false;
  }
  
  struct dir *parent_dir;
  parent_dir = dir_open(parent);
  
  bool success = dir_remove(parent_dir, dir_name);
  dir_close(dir);
  dir_close(parent_dir);
  return success;
}

bool filesys_isdir(const char *path)
{
  struct inode *child;

  if(!path_lookup(path, true, NULL, NULL, &child))
    return false;
  
  if(!inode_isdir(child)){
    inode_close(child);
    return false;
  }
  
  inode_close(child);
  return true;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();

  struct dir *root_dir = dir_open_root();
  dir_add(root_dir, ".", ROOT_DIR_SECTOR);
  dir_add(root_dir, "..", ROOT_DIR_SECTOR);
  dir_close(root_dir);
  printf ("done.\n");
}

/* 
  Used to lookup an absolute or relative path.
  
  If the must_exist flag is true then the specified file/directory must be
  present. If false, the file/directory must not be present.

  Returns true if successful, false if not.

  If name, parent, or child are not null, then their values are set to the
  given file/direcory name, the parent directory's inode, and the specified
  file/directory's inode respectively.  
*/
bool path_lookup(const char *dir, bool must_exist, char **name,
    struct inode **parent, struct inode **child)
{
  if(strlen(dir) == 0)
    return false;
  
  if(thread_current()->cwd == NULL){
    thread_current()->cwd = dir_open_root();
  }

  bool absolute = dir[0] == '/';

  if(strlen(dir) == 1 && absolute && must_exist){
    struct dir *root = dir_open_root();
    if(parent != NULL)
      *parent = inode_reopen(root->inode);
    if(child != NULL)
      *child = inode_reopen(root->inode);
    if(name != NULL)
      *name = "/";
    dir_close(root);
    return true;
  }

  struct dir *current_directory = absolute ? dir_open_root() 
    : dir_reopen(thread_current()->cwd);

  struct inode *parent_inode = inode_reopen(current_directory->inode);
  struct inode *current_inode = inode_reopen(parent_inode);

  char *saveptr;
  char *token = NULL;
  char *next_token = NULL;
  
  token = strtok_r(dir, "/", &saveptr);
  while (token != NULL)
  {
    next_token = strtok_r(NULL, "/", &saveptr);
    if (next_token == NULL)
    {
      inode_close(current_inode);
      bool exists = dir_lookup(current_directory, token, &current_inode);
      if ((exists && !must_exist) || (!exists && must_exist))
      {
        inode_close(current_inode);
        dir_close(current_directory);
        inode_close(parent_inode);
        return false;
      }
      else
      {
        if(parent != NULL)
          *parent = inode_reopen(parent_inode);
        if(child != NULL)
          *child = inode_reopen(current_inode);
        if(name != NULL)
          *name = token;
        inode_close(parent_inode);
        inode_close(current_inode);
        dir_close(current_directory);
        return true;
      }
    }
    inode_close(current_inode);
    if (!dir_lookup(current_directory, token, &current_inode)){
      dir_close(current_directory);
      inode_close(parent_inode);
      return false;
    }

    inode_close(parent_inode);
    parent_inode = inode_reopen(current_inode);
    dir_close(current_directory);
    current_directory = dir_open(inode_reopen(current_inode));

    token = next_token;
  }
  dir_close(current_directory);
  inode_close(parent_inode);
  return false;
}
