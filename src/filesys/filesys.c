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

static void do_format(void);
bool path_lookup(const char *, bool, char **, struct inode **,
                 struct inode **);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init(bool format)
{
  fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC("No file system device found, can't initialize file system.");

  inode_init();
  free_map_init();

  if (format)
    do_format();

  free_map_open();
}

//Tarun is driving
/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done(void)
{
  free_map_close();
}

/* Creates a file given path with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if specified file name already exists,
   or if internal memory allocation fails. */
bool filesys_create(const char *path, off_t initial_size)
{
  char *name;
  struct inode *parent_inode;

  //Look up path, and make sure file is not already there, and path
  //is valid
  if (!path_lookup(path, false, &name, &parent_inode, NULL))
    return false;

  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open(parent_inode);
  //Obtain directory lock to create file
  inode_acquire_dirlock(dir->inode);
  bool success = (dir != NULL && free_map_allocate(1, &inode_sector) && 
    inode_create(inode_sector, initial_size, false) && 
      dir_add(dir, name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);
  inode_release_dirlock(dir->inode);
  dir_close(dir);
  return success;
}

/* Opens the file with the given path.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open(const char *path)
{
  char *name;
  struct inode *child_inode;

  //Ensures that path is valid, and file is actually there
  if (!path_lookup(path, true, &name, NULL, &child_inode))
    return NULL;

  //Make sure we're opening file, not directory
  if (inode_isdir(child_inode))
  {
    inode_close(child_inode);
    return NULL;
  }

  return file_open(child_inode);
}

/* Opens directory with given path,
   returns directory if opened, else
   return NULL
*/
struct dir *
filesys_opendir(const char *path)
{
  char *name;
  struct inode *child_inode;
  //Ensures that path is valid and directory is there
  if (!path_lookup(path, true, &name, NULL, &child_inode))
    return NULL;
  //Ensures that inode is referencing directory
  if (!inode_isdir(child_inode))
  {
    inode_close(child_inode);
    return NULL;
  }
  return dir_open(child_inode);
}

/* Deletes the file given path.
   Returns true if successful, false on failure.
   Fails if specified file doesn't exist,
   or if an internal memory allocation fails. */
bool filesys_remove(const char *path)
{
  char *name;
  struct inode *parent_inode;
  //Ensures that path is valid, and file is actually there
  if (!path_lookup(path, true, &name, &parent_inode, NULL))
    return false;

  struct dir *dir = dir_open(parent_inode);

  inode_acquire_dirlock(dir->inode);
  bool success = dir != NULL && dir_remove(dir, name);
  inode_release_dirlock(dir->inode);

  dir_close(dir);
  return success;
}

/* Creates a directory given path, and makes
   directory, and returns if creation was successful
   or not
*/
//Aaron is driving
bool filesys_mkdir(const char *path)
{
  struct inode *parent_inode;
  char *name;

  //Ensures that path is valid, and directory is not there
  if (!path_lookup(path, false, &name, &parent_inode, NULL))
    return false;

  block_sector_t sector;

  //Finds sector to allocate to
  if (!free_map_allocate(1, &sector))
    return false;

  struct dir *parent_directory;
  struct dir *new_directory;

  dir_create(sector, 2);
  parent_directory = dir_open(parent_inode);
  //Tarun is driving

  //Populate directory with "." and ".." to point to the
  //directory and parent directory respectively
  new_directory = dir_open(inode_open(sector));
  dir_add(new_directory, ".", sector);
  dir_add(new_directory, "..", inode_get_inumber(parent_directory->inode));
  dir_close(new_directory);

  //Adds directory to parent directory
  dir_add(parent_directory, name, sector);
  dir_close(parent_directory);

  return true;
}

/* Changes cwd to new directory given path, and
   returns if it was successful or not
*/
bool filesys_chdir(const char *path)
{
  struct inode *child;

  //Ensures that path is valid, and directory is valid
  if (!path_lookup(path, true, NULL, NULL, &child))
    return false;

  //Close old cwd
  dir_close(thread_current()->cwd);

  //Set new cwd to new directory
  thread_current()->cwd = dir_open(child);
  return true;
}

//Chris is driving
/* Remove directory based on given path,
   and returns if it was successful or not
*/
bool filesys_rmdir(const char *path)
{
  struct inode *child;
  struct inode *parent;
  char *dir_name;

  //Ensures that path is valid, and that directory is there
  if (!path_lookup(path, true, &dir_name, &parent, &child))
    return false;

  //Ensures that we are removing directory, not file
  if (!inode_isdir(child))
  {
    inode_close(parent);
    inode_close(child);
    return false;
  }

  struct dir *dir;
  dir = dir_open(child);

  //Ensures that we are not deleting the current working directory
  if (inode_get_inumber(dir->inode) ==
      inode_get_inumber(thread_current()->cwd->inode))
  {
    dir_close(dir);
    return false;
  }

  //Ensures that we are not removing directory being
  //used by another process
  if (inode_isopen(dir->inode))
  {
    dir_close(dir);
    return false;
  }
  //Henry is driving
  //If readdir successful, we don't remove the
  //directory
  char name[NAME_MAX + 1];
  if (dir_readdir(dir, name))
  {
    dir_close(dir);
    return false;
  }

  struct dir *parent_dir;
  parent_dir = dir_open(parent);
  //If no conditions met above, we remove the directory from
  //the parent directory
  bool success = dir_remove(parent_dir, dir_name);
  dir_close(dir);
  dir_close(parent_dir);
  return success;
}

/* Uses specified path to determine if dir is in fact
   a directory, and returns if it is or not
*/
bool filesys_isdir(const char *path)
{
  struct inode *child;

  //Ensures path is valid
  if (!path_lookup(path, true, NULL, NULL, &child))
    return false;

  //If not a directory, return false
  if (!inode_isdir(child))
  {
    inode_close(child);
    return false;
  }

  inode_close(child);
  return true;
}

/* Formats the file system. */
static void
do_format(void)
{
  printf("Formatting file system...");
  free_map_create();
  if (!dir_create(ROOT_DIR_SECTOR, 16))
    PANIC("root directory creation failed");
  free_map_close();

  struct dir *root_dir = dir_open_root();
  dir_add(root_dir, ".", ROOT_DIR_SECTOR);
  dir_add(root_dir, "..", ROOT_DIR_SECTOR);
  dir_close(root_dir);
  printf("done.\n");
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
  //Path is not valid, since empty
  if (strlen(dir) == 0)
    return false;
  //Chris is driving
  //No current working directory, so set it to root
  if (thread_current()->cwd == NULL)
  {
    thread_current()->cwd = dir_open_root();
  }

  bool absolute = dir[0] == '/';
  //Conditions to check if we need to set to parent or
  //child inode to root
  if (strlen(dir) == 1 && absolute && must_exist)
  {
    struct dir *root = dir_open_root();
    if (parent != NULL)
      *parent = inode_reopen(root->inode);
    if (child != NULL)
      *child = inode_reopen(root->inode);
    if (name != NULL)
      *name = "/";
    dir_close(root);
    return true;
  }
  //Aaron is driving
  //Uses identifer '/' to determine if doing absolute or relative:
  //if absolute, use root, else use cwd
  struct dir *current_directory = absolute ? dir_open_root()
                                           : dir_reopen(thread_current()->cwd);

  struct inode *parent_inode = inode_reopen(current_directory->inode);
  struct inode *current_inode = inode_reopen(parent_inode);

  char *saveptr;
  char *token = NULL;
  char *next_token = NULL;

  token = strtok_r(dir, "/", &saveptr);
  //While we still have more tokens left, spaced with "/"
  while (token != NULL)
  {
    next_token = strtok_r(NULL, "/", &saveptr);
    //This means we are at the last token
    if (next_token == NULL)
    {
      //Henry is driving
      inode_close(current_inode);
      //Look up last token
      bool exists = dir_lookup(current_directory, token, &current_inode);
      //If it exists and must not exist or vice versa, conditions failed,
      //and return false
      if ((exists && !must_exist) || (!exists && must_exist))
      {
        inode_close(current_inode);
        dir_close(current_directory);
        inode_close(parent_inode);
        return false;
      }
      //Conditions are true, set parent and child inode to respective
      //values
      else
      {
        //Tarun is driving
        if (parent != NULL)
          *parent = inode_reopen(parent_inode);
        if (child != NULL)
          *child = inode_reopen(current_inode);
        if (name != NULL)
          *name = token;
        inode_close(parent_inode);
        inode_close(current_inode);
        dir_close(current_directory);
        return true;
      }
    }
    inode_close(current_inode);
    //If directory in given path is not valid, return false
    if (!dir_lookup(current_directory, token, &current_inode))
    {
      dir_close(current_directory);
      inode_close(parent_inode);
      return false;
    }
    //Chris is driving
    //Close parent inode and set to new current_inode as we
    //traverse
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
