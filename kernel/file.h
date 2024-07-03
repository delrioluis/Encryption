struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref; // reference count
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE

  //bellow is the corresponding iNode iNode has 
  struct inode *ip;  // FD_INODE and FD_DEVICE

  uint off;          // FD_INODE
  short major;       // FD_DEVICE


  //start
  //uint8 encrypted;
  //end 
  
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// in-memory copy of an inode
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?

  //start
  //unencrypted is value of 0
  //encrypted is a value of 1
  uint8 encrypted;    //this isnt part of the disk iNode

  //end

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  //how do we use addrs ? its a bunch of unsigned integers which are adresses
  uint addrs[NDIRECT+1];


};

// map major device number to device functions.
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
