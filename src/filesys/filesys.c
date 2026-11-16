#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/thread.h"
#include "userprog/process.h"

/* Partition that contains the file system. */
struct block* fs_device;

static void do_format(void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init(bool format) {
  fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC("No file system device found, can't initialize file system.");

  cache_init();
  inode_init();
  free_map_init();

  if (format)
    do_format();

  free_map_open();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done(void) {
  free_map_close();
  cache_flush();
}

/* The boot kernel has no cwd until the file system is mounted. */
struct dir* filesys_cwd_reopen(void) {
  struct process* pcb = thread_current()->pcb;
  return pcb != NULL && pcb->cwd != NULL ? dir_reopen(pcb->cwd) : dir_open_root();
}

static int next_part(char name[NAME_MAX + 1], const char** cursor) {
  const char* src = *cursor;
  while (*src == '/')
    ++src;
  size_t n = 0;
  while (*src && *src != '/') {
    if (n == NAME_MAX)
      return -1;
    name[n++] = *src++;
  }
  name[n] = 0;
  *cursor = src;
  return n ? 1 : 0;
}

/* Walk every intermediate component as a real directory (including ./..).
   No full-path copy or recursion is needed, so only components are bounded. */
static struct dir* parent_dir(const char* path, char leaf[NAME_MAX + 1]) {
  if (path == NULL || !*path)
    return NULL;
  struct dir* dir = *path == '/' ? dir_open_root() : filesys_cwd_reopen();
  if (!dir)
    return NULL;
  const char* cursor = path;
  int result = next_part(leaf, &cursor);
  if (result < 0)
    goto fail;
  if (result == 0)
    return dir; /* All slashes denote root. */
  for (;;) {
    char next[NAME_MAX + 1];
    const char* after = cursor;
    result = next_part(next, &after);
    if (result < 0)
      goto fail;
    if (result == 0)
      return dir;
    struct inode* child = NULL;
    if (!dir_lookup(dir, leaf, &child))
      goto fail;
    struct dir* descended = dir_open(child);
    dir_close(dir);
    dir = descended;
    if (!dir)
      return NULL;
    strlcpy(leaf, next, NAME_MAX + 1);
    cursor = after;
  }
fail:
  dir_close(dir);
  return NULL;
}

static bool trailing_slash(const char* name) { return *name && name[strlen(name) - 1] == '/'; }

static struct inode* resolve(const char* path) {
  char leaf[NAME_MAX + 1];
  struct dir* dir = parent_dir(path, leaf);
  if (!dir)
    return NULL;
  struct inode* inode = NULL;
  if (!*leaf)
    inode = inode_reopen(dir_get_inode(dir));
  else
    dir_lookup(dir, leaf, &inode);
  dir_close(dir);
  if (inode && trailing_slash(path) && !inode_is_dir(inode)) {
    inode_close(inode);
    inode = NULL;
  }
  return inode;
}

static bool create_path(const char* name, off_t initial_size, bool directory) {
  char leaf[NAME_MAX + 1];
  struct dir* dir = parent_dir(name, leaf);
  if (!dir)
    return false;
  bool success = false;
  block_sector_t sector;
  bool made = false;
  if (*leaf && strcmp(leaf, ".") && strcmp(leaf, "..") && (directory || !trailing_slash(name)) &&
      free_map_allocate(1, &sector)) {
    made = directory ? dir_create(sector, 16, inode_get_inumber(dir_get_inode(dir)))
                     : inode_create(sector, initial_size);
    success = made && dir_add(dir, leaf, sector);
    if (!success) {
      if (made)
        inode_discard(sector);
      else
        free_map_release(sector, 1);
    }
  }
  dir_close(dir);
  return success;
}

bool filesys_create(const char* name, off_t initial_size) {
  return create_path(name, initial_size, false);
}

/* Opens the file or directory identified by a full path. */
struct file* filesys_open(const char* name) { return file_open(resolve(name)); }
bool filesys_mkdir(const char* name) { return create_path(name, 0, true); }
bool filesys_chdir(const char* name) {
  struct dir* next = dir_open(resolve(name));
  if (!next)
    return false;
  struct process* pcb = thread_current()->pcb;
  dir_close(pcb->cwd);
  pcb->cwd = next;
  return true;
}
bool filesys_remove(const char* name) {
  char leaf[NAME_MAX + 1];
  struct dir* dir = parent_dir(name, leaf);
  if (!dir)
    return false;
  bool valid = *leaf != 0;
  if (valid && trailing_slash(name)) {
    struct inode* target = NULL;
    valid = dir_lookup(dir, leaf, &target) && inode_is_dir(target);
    inode_close(target);
  }
  bool success = valid && dir_remove(dir, leaf);
  dir_close(dir);
  return success;
}

/* Formats the file system. */
static void do_format(void) {
  printf("Formatting file system...");
  free_map_create();
  if (!dir_create(ROOT_DIR_SECTOR, 16, ROOT_DIR_SECTOR))
    PANIC("root directory creation failed");
  free_map_close();
  printf("done.\n");
}
