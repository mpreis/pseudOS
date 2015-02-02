#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);
static void filesys_split_filepath (const char *filepath, char **path, char **name);

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

  thread_current ()->cwd = dir_open_root ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  cache_flush ();
  free_map_close ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir) 
{
  if (name == NULL || strlen (name) == 0 || strlen (name) > NAME_MAX)
    return false;

  block_sector_t inode_sector = 0;
  char *path, *filename;
  filesys_split_filepath (name, &path, &filename);

  struct dir *dir = (path == NULL)
    ? dir_reopen (thread_current ()->cwd)
    : dir_get_dir (path);

  struct inode *dir_inode = dir_get_inode (dir);  
  struct inode *tmp_inode;
  
  if (dir == NULL || dir_lookup (dir, filename, &tmp_inode))
    return false;

  if (!free_map_allocate (1, &inode_sector))
    return false;

  if (is_dir)
  {
    if (!dir_create (inode_sector, inode_get_inumber (dir_inode), initial_size))
      return false;
  }
  else
  {
    if (!inode_create (inode_sector, initial_size, is_dir))
      return false;
  }
  bool tmp = dir_add (dir, filename, inode_sector);

  inode_close (tmp_inode);
  dir_close (dir);
  return true;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  if (name == NULL || strlen (name) == 0 || strlen (name) > NAME_MAX)
    return NULL;

  char *path, *filename;
  filesys_split_filepath (name, &path, &filename);

  struct dir *dir = dir_get_dir (path);
  struct inode *inode = NULL;

  if (dir != NULL)
  {
    if (strcmp (filename, "") == 0)
      return file_open (dir->inode);

    dir_lookup (dir, filename, &inode);

    dir_close (dir);
    return file_open (inode);
  }
  return NULL;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}


static void
filesys_split_filepath (const char *filepath, char **path, char **name)
{
  int fp_length = strlen (filepath);
  char *fp_copy = malloc (fp_length + 1);
  strlcpy (fp_copy, filepath, fp_length + 1);

  if (fp_copy[fp_length] == '/')
    fp_copy[fp_length] = '\0';

  char *last_sep_pos = strrchr (fp_copy, '/');
  if (last_sep_pos == NULL)
  {
    *path = "";
    *name = malloc (fp_length + 1);
    strlcpy (*name, fp_copy, fp_length + 1);
  }
  else if (last_sep_pos == fp_copy)
  {
    *path = "/";
    *name = malloc (fp_length);
    strlcpy (*name, fp_copy + sizeof(char), fp_length);
  }
  else
  {
    unsigned path_length = last_sep_pos - fp_copy;
    *path = malloc (path_length + 1);
    strlcpy (*path, fp_copy, path_length + 1);

    unsigned name_length = fp_length - path_length;
    *name = malloc (name_length + 1);
    strlcpy (*name, last_sep_pos + 1, name_length + 1);
  }
  free (fp_copy);
}
