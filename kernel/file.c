//
// Support functions for system calls that involve file descriptors.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"

struct devsw devsw[NDEV];

// file descriptor table or ftable
struct
{
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Allocate a file structure.
struct file *
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for (f = ftable.file; f < ftable.file + NFILE; f++)
  {
    if (f->ref == 0)
    {
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Increment ref count for file f.
struct file *
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if (f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Close file f.  (Decrement ref count, close when reaches 0.)
void fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if (f->ref < 1)
    panic("fileclose");
  if (--f->ref > 0)
  {
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if (ff.type == FD_PIPE)
  {
    pipeclose(ff.pipe, ff.writable);
  }
  else if (ff.type == FD_INODE || ff.type == FD_DEVICE)
  {
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Get metadata about file f.
// addr is a user virtual address, pointing to a struct stat.
int filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;

  if (f->type == FD_INODE || f->type == FD_DEVICE)
  {
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if (copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Read from file f.
// addr is a user virtual address.
int fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if (f->readable == 0)
    return -1;

  if (f->type == FD_PIPE)
  {
    r = piperead(f->pipe, addr, n);
  }
  else if (f->type == FD_DEVICE)
  {
    if (f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  }
  else if (f->type == FD_INODE)
  {
    ilock(f->ip);
    if ((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  }
  else
  {
    panic("fileread");
  }

  return r;
}

// Write to file f.
// addr is a user virtual address.
int filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if (f->writable == 0)
    return -1;

  if (f->type == FD_PIPE)
  {
    ret = pipewrite(f->pipe, addr, n);
  }
  else if (f->type == FD_DEVICE)
  {
    if (f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  }
  else if (f->type == FD_INODE)
  {
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;
    int i = 0;
    while (i < n)
    {
      int n1 = n - i;
      if (n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if (r != n1)
      {
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  }
  else
  {
    panic("filewrite");
  }

  return ret;
}

// start
//   WHAT DO WE KNOW
//   fd is the file descriptor
//   key is the encryption/decryption key
//   - will be applied using an XOR technique
//   - the key is an uint8 (unsigned integer 8 bit)
//   - 8 bits is one byte so we are going to apply it byte-by-byte
//   WHAT I DONT KNOW
//   - how do i get acces to each individual byte in a file
//   - fd is the file descriptor, but its only an integer
//   -

// what should be locked
// ftable ?
// inode ?

int encrypt(int fd, uint8 key)
{
  // get pointer to a file struct
  // file struct will be located on the ftable
  acquire(&ftable.lock);
  struct file *f;
  if ((f = myproc()->ofile[fd]) == 0)
  {
    release(&ftable.lock);
    return -1;
  }
  // file should be referenced an check for correct type
  if (f->ref == 0 || f->type != 2)
  {
    release(&ftable.lock);
    return -1;
  }
  // file should be readable and writable
  if (f->readable == 0 || f->writable == 0)
  {
    release(&ftable.lock);
    return -1;
  }

  // get inode pointer
  struct inode *ip = f->ip;
  ilock(ip);
  // iNode must be valid
  if (ip->valid == 0)
  {
    iunlock(ip);
    return -1;
  }
  // can only work on unencrypted files
  if (ip->encrypted == 1)
  {
    iunlock(ip);
    release(&ftable.lock);
    return -1;
  }

  // we can read the iNode here
  // i believe we use readi
  // use readi here save the data
  uchar data[ip->size];
  // might not need offset
  int bytesRead = readi(ip, 0, (uint64)data, 0, ip->size);

  if (bytesRead == 0)
  {
    iunlock(ip);
    release(&ftable.lock);

    return -1;
  }

  // encrypt data
  for (int k = 0; k < ip->size; k++)
  {
    uchar d = data[k];
    data[k] = d ^ key;
  }

  int bytesWritten = writei(ip, 0, (uint64)data, 0, ip->size);

  if (bytesWritten != bytesRead)
  {

    iunlock(ip);
    release(&ftable.lock);
    return -1;
  }
  else
  {
    ip->encrypted = 1;
    iupdate(ip);
  }

  iunlock(ip);
  release(&ftable.lock);
  return 0;
}

int decrypt(int fd, uint8 key)
{
  // get pointer to a file struct
  // file struct will be located on the ftable
  acquire(&ftable.lock);
  struct file *f;
  if ((f = myproc()->ofile[fd]) == 0)
  {
    release(&ftable.lock);
    return -1;
  }
  // file should be referenced an check for correct type
  if (f->ref == 0 || f->type != 2)
  {
    release(&ftable.lock);
    return -1;
  }
  // file should be readable and writable
  if (f->readable == 0 || f->writable == 0)
  {
    release(&ftable.lock);
    return -1;
  }

  // get inode pointer
  struct inode *ip = f->ip;
  ilock(ip);
  // iNode must be valid
  if (ip->valid == 0)
  {
    iunlock(ip);
    return -1;
  }
  // can only work on encrypted files
  if (ip->encrypted == 0)
  {
    iunlock(ip);
    release(&ftable.lock);
    return -1;
  }

  // we can read the iNode here
  // i believe we use readi
  // use readi here save the data
  uchar data[ip->size];
  // might not need offset
  int bytesRead = readi(ip, 0, (uint64)data, 0, ip->size);

  if (bytesRead == 0)
  {
    iunlock(ip);
    release(&ftable.lock);

    return -1;
  }

  // de-encrypt data
  for (int k = 0; k < ip->size; k++)
  {
    uchar d = data[k];
    data[k] = d ^ key;
  }

  int bytesWritten = writei(ip, 0, (uint64)data, 0, ip->size);

  if (bytesWritten != bytesRead)
  {

    iunlock(ip);
    release(&ftable.lock);
    return -1;
  }
  else
  {
    ip->encrypted = 1;
    iupdate(ip);
  }

  iunlock(ip);
  release(&ftable.lock);
  return 0;
}
// end
