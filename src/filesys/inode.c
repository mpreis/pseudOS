#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define NR_OF_DIRECT 12
#define NR_OF_INDIRECT 128
#define MAX_FILE_SECTORS NR_OF_DIRECT + NR_OF_INDIRECT + NR_OF_INDIRECT * NR_OF_INDIRECT
#define SECTOR_FAILED -1

/* On-disk inode. Unix like structure. 
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
  /* meta */
  off_t test;
  off_t length;                             /* File size in bytes. */
  block_sector_t direct[NR_OF_DIRECT];      /* 12 * BLOCK_SECTOR_SIZE = 6144 bytes. */
  block_sector_t single_indirect;           /* 128 * BLOCK_SECTOR_SIZE = 65536 bytes. */
  block_sector_t double_indirect;           /* 128 * 128 * BLOCK_SECTOR_SIZE = 8388608 bytes. */
  unsigned magic;                           /* Magic number. */
  uint32_t unused [NR_OF_INDIRECT - NR_OF_DIRECT - 5];
};

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;                /* Element in inode list. */
    block_sector_t sector;                /* Sector number of disk location. */
    int open_cnt;                         /* Number of openers. */
    bool removed;                         /* True if deleted, false otherwise. */
    int deny_write_cnt;                   /* 0: writes ok, >0: deny writes. */

    off_t length;                         /* File size in bytes. */
    block_sector_t direct[NR_OF_DIRECT];  /* 12 * BLOCK_SECTOR_SIZE = 6144 bytes. */
    block_sector_t single_indirect;       /* 128 * BLOCK_SECTOR_SIZE = 65536 bytes. */
    block_sector_t double_indirect;       /* 128 * 128 * BLOCK_SECTOR_SIZE = 8388608 bytes. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos > inode->length)
    return SECTOR_FAILED;

  block_sector_t sector_idx = SECTOR_FAILED;
  int inode_idx = (pos == 0) ? 0 : DIV_ROUND_UP (pos, BLOCK_SECTOR_SIZE) -1;

  if (inode_idx < NR_OF_DIRECT)
  {
    sector_idx = inode->direct[inode_idx];
    printf(" --- byte_to_sector: inode_idx: %d inode->direct[]: %d \n", inode_idx, inode->direct[inode_idx]);
  }
  else if (inode_idx < NR_OF_DIRECT + NR_OF_INDIRECT)
  {
    block_sector_t single_indirect[NR_OF_INDIRECT];
    cache_read_sector (single_indirect, inode->single_indirect);
    sector_idx = single_indirect[inode_idx - NR_OF_DIRECT];
  }
  else if(inode_idx < MAX_FILE_SECTORS)
  {
    block_sector_t double_indirect[NR_OF_INDIRECT];
    cache_read_sector (double_indirect, inode->double_indirect);

    unsigned double_indirect_idx = (inode_idx - NR_OF_DIRECT - NR_OF_INDIRECT) / NR_OF_INDIRECT;
    block_sector_t single_indirect[NR_OF_INDIRECT];
    cache_read_sector (&single_indirect, double_indirect[double_indirect_idx]);

    unsigned single_indirect_idx = (inode_idx - NR_OF_DIRECT - NR_OF_INDIRECT) % NR_OF_INDIRECT;
    sector_idx = single_indirect[single_indirect_idx];
  }

  return sector_idx;
}

static block_sector_t
inode_allocate_ind_sectors (block_sector_t sector, int old_idx, int new_idx, int bound)
{
  int i;

  if(old_idx < bound)
  {
    free_map_allocate (1, &sector);
    old_idx = bound - 1;
  }

  block_sector_t single_indirect[NR_OF_INDIRECT];
  cache_read_sector (single_indirect, sector);

  for (i = old_idx - (bound - 1); 
       i <= (new_idx - bound) && i < NR_OF_INDIRECT; i++)
  {
    block_sector_t tmp_sector_idx;
    free_map_allocate (1, &tmp_sector_idx);
    single_indirect[i] = tmp_sector_idx;
  }
  cache_write_sector (single_indirect, sector);

  return sector;
}

static block_sector_t
inode_allocate_d_ind_sectors (struct inode *inode, int old_idx, 
                            int new_idx, int bound)
{
  block_sector_t sector = inode->sector;
  int i;

  if(old_idx < bound)
  {
    free_map_allocate (1, &sector);
    old_idx = bound - 1;
  }

  block_sector_t double_indirect[NR_OF_INDIRECT];
  cache_read_sector (double_indirect, sector);
  
  int old_d_ind_idx = (old_idx - (bound - 1)) / NR_OF_INDIRECT;
  int new_d_ind_idx = (new_idx - (bound - 1)) / NR_OF_INDIRECT;

  for (i = old_d_ind_idx; i <= new_d_ind_idx && i < NR_OF_INDIRECT; i++)
    double_indirect[i] = inode_allocate_ind_sectors (inode->sector, old_idx, new_idx, 
                          NR_OF_DIRECT + NR_OF_INDIRECT + i * NR_OF_INDIRECT);
  
  cache_write_sector (double_indirect, sector);
  return sector;
}

static block_sector_t
inode_allocate_sectors (struct inode *inode, off_t pos)
{
  block_sector_t sector_idx = SECTOR_FAILED;
  int new_idx = (pos == 0) ? 0 : DIV_ROUND_UP (pos, BLOCK_SECTOR_SIZE) - 1;
  int old_idx = (inode->length == 0) ? -1 : DIV_ROUND_UP (inode->length, BLOCK_SECTOR_SIZE) - 1;
  int i;

  ASSERT(old_idx < new_idx);

  struct inode_disk *disk_inode = calloc (1, sizeof *disk_inode);
  disk_inode->magic = INODE_MAGIC;
  disk_inode->length = inode->length;
  disk_inode->single_indirect = inode->single_indirect;
  disk_inode->double_indirect = inode->double_indirect;

  if (old_idx < NR_OF_DIRECT)
  {
    for (i = 0; i <= new_idx && i < NR_OF_DIRECT; i++)
    {
      if(i <= old_idx)
        disk_inode->direct [i] = inode->direct[i];
      else
      {
        free_map_allocate (1, &sector_idx);
        disk_inode->direct [i] = sector_idx;
      }
    }
  }

  if (old_idx < NR_OF_DIRECT + NR_OF_INDIRECT 
      && new_idx >=  NR_OF_DIRECT)
  {
    disk_inode->single_indirect = inode_allocate_ind_sectors (inode->sector, old_idx, 
                                                              new_idx, NR_OF_DIRECT);
  }
  
  if (old_idx < MAX_FILE_SECTORS
      && new_idx >= (NR_OF_DIRECT + NR_OF_INDIRECT))
  {
    disk_inode->double_indirect = inode_allocate_d_ind_sectors (inode, old_idx, new_idx, 
                                                                NR_OF_DIRECT + NR_OF_INDIRECT);
  }

  cache_write_sector (disk_inode, inode->sector);
  inode->length = pos;
  inode->single_indirect = disk_inode->single_indirect;
  inode->double_indirect = disk_inode->double_indirect;
  free (disk_inode);

  return sector_idx;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  struct inode_disk *tmp_disk_inode_2 = calloc (1, sizeof *disk_inode);
  cache_read_sector (tmp_disk_inode_2, 0);
  printf(" --- inode_create --- len:%d test: %d\n", tmp_disk_inode_2->length,tmp_disk_inode_2->test);


  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  // ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);
  //   printf("\n --- inode_create: sector: %d length: %d \n", sector, length);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
  {
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;
    disk_inode->test = 12345678;

    struct inode *inode;
    inode = malloc (sizeof *inode);
    if (inode == NULL) return false;
    inode->sector = sector;
    inode->open_cnt = 1;
    inode->deny_write_cnt = 0;
    inode->removed = false;
    inode->length = 0;

    free_map_allocate (1, &inode->direct[0]);

    char *tmp = "dummy";
    inode_write_at(inode, tmp, 1, disk_inode->length);

    disk_inode->single_indirect = inode->single_indirect;
    disk_inode->double_indirect = inode->double_indirect;
    disk_inode->direct[0] = inode->direct[0];

    printf(" --- inode_create: disk_inode->len: %d\n", disk_inode->length);

    cache_write_sector (disk_inode, sector);
    success = true; 
    free (disk_inode);
  }



  struct inode_disk *tmp_disk_inode = calloc (1, sizeof *disk_inode);
  cache_read_sector (tmp_disk_inode, sector);
  printf(" --- inode_create --- len:%d test: %d\n", tmp_disk_inode->length,tmp_disk_inode->test);





  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;

  struct inode_disk *disk_inode = NULL;
  disk_inode = calloc (1, sizeof *disk_inode);
  if(disk_inode)
  {
    cache_read_sector (disk_inode, inode->sector);
    printf(" --- inode_open: disk_inode->len: %d\n",disk_inode->length);
    inode->length = disk_inode->length;

    inode->single_indirect = disk_inode->single_indirect;
    inode->double_indirect = disk_inode->double_indirect;
    memcpy (&inode->direct, &disk_inode->direct, NR_OF_DIRECT * sizeof (block_sector_t));
    free (disk_inode);
  }

    printf(" --- inode_open: sector: %d inode->len: %d\n", inode->sector, inode->length);

  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

static void
inode_free_sectors (struct inode *inode)
{
  int idx = (inode->length == 0) ? 0 : DIV_ROUND_UP (inode->length, BLOCK_SECTOR_SIZE) - 1;
  int i;

  // free directs
  for (i = 0; i <= idx && i < NR_OF_DIRECT; i++)
    free_map_release (inode->direct[i], 1);

  // free single indirects
  block_sector_t single_indirect[NR_OF_INDIRECT];
  cache_read_sector(single_indirect, inode->single_indirect);

  for (i = 0; i <= idx - NR_OF_DIRECT && i < NR_OF_INDIRECT; i++)
    free_map_release (single_indirect[i], 1);

  free_map_release (inode->single_indirect, 1);

  // free double indirects
  block_sector_t double_indirect[NR_OF_INDIRECT];
  cache_read_sector(double_indirect, inode->double_indirect);

  int double_indirect_idx = (idx - (NR_OF_DIRECT + NR_OF_INDIRECT - 1)) / NR_OF_INDIRECT;  

  for (i = 0; i <= double_indirect_idx && i < NR_OF_INDIRECT; i++)
  {
    cache_read_sector(single_indirect, double_indirect[i]);

    int j;
    for (j = 0; j <= idx - (NR_OF_DIRECT + NR_OF_INDIRECT + i * NR_OF_INDIRECT) 
                && j < NR_OF_INDIRECT; j++)
      free_map_release (single_indirect[j], 1);

    free_map_release (double_indirect[i], 1);
  }

  free_map_release (inode->double_indirect, 1);

  // free disk_inode sector
  free_map_release (inode->sector, 1);
}


/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  printf(" --- inode_close sector: %d \n", inode->sector); 
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
  {
    printf(" -1- inode_close\n"); 
    /* Remove from inode list and release lock. */
    list_remove (&inode->elem);

    /* Deallocate blocks if removed. */
    if (inode->removed) 
    {
      printf(" -2- inode_close\n"); 
      inode_free_sectors(inode);
    }  

    free (inode); 
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  if(offset >= inode->length)
    return bytes_read;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);

      printf(" --- inode_read_at: %d \n", sector_idx);
      if(sector_idx == SECTOR_FAILED)
        return bytes_read;

      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      printf(" --- inode_read_at: min_left: %d size: %d inode_left: %d sector_left: %d inode->length: %d\n", 
        min_left, size, inode_left, sector_left, inode->length);

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      printf(" --- inode_read_at: buf:%p sector_idx: %d sector_ofs: %d chunk_size: %d bytes_read: %d \n", 
        (void *)buffer, sector_idx, sector_ofs, chunk_size, bytes_read);
      cache_read ((void *)buffer, sector_idx, sector_ofs, chunk_size, bytes_read);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{

  struct inode_disk *tmp_disk_inode_2 = NULL;
  tmp_disk_inode_2 = calloc (1, sizeof *tmp_disk_inode_2);
  cache_read_sector (tmp_disk_inode_2, 0);
  printf(" -1- inode_write_at --- sector: %d offset: %d size: %d len:%d test: %d\n", 
    inode->sector, offset, size, tmp_disk_inode_2->length, tmp_disk_inode_2->test);

  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  if(offset+size > inode_length(inode))
  { 
    inode_allocate_sectors (inode, offset + size);
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset + size);
      // if(sector_idx == SECTOR_FAILED)
      // { 
      //   sector_idx = inode_allocate_sectors (inode, offset + size);

      //   if(sector_idx == SECTOR_FAILED)
      //     return bytes_written;
      // }

      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

  struct inode_disk *tmp_disk_inode = NULL;
  tmp_disk_inode = calloc (1, sizeof *tmp_disk_inode);
  cache_read_sector (tmp_disk_inode, 0);
  printf(" -2- inode_write_at --- sector: %d len: %d test: %d\n", sector_idx, tmp_disk_inode->length, tmp_disk_inode->test);

      // overwrites data
      cache_write ((void *)buffer, sector_idx, sector_ofs, chunk_size, bytes_written);


      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->length;
}
