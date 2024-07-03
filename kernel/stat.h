#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device


//we might use this to get the iNode once we have the file descriptor
  //the iNode tells us if we have an encrypted file or not
struct stat {
  int dev;     // File system's disk device
  uint ino;    // Inode number
  short type;  // Type of file
  short nlink; // Number of links to file
  uint64 size; // Size of file in bytes
  uint8 encrypted;
};
