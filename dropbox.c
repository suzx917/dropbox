#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define BLOCK_NUM 4226
#define BLOCK_SIZE 8192
#define MAX_FILE_NUM 128 // determines the number of dir entries and inodes
#define INODE_BLOCK_NUM 1250 // determines the number of blocks each inode can have
#define MAX_FILE_SIZE 1250*8192

struct Directory_Entry { // Entry ~ 2
  uint8_t  valid;
  char     name[255];
  uint32_t inode;
};

struct Inode { // Inode ~ 5 KB
  uint8_t  attribute;
  uint32_t blocks[INODE_BLOCK_NUM]; // max file size ~ 10 MB
  uint32_t size;
};

uint8_t blocks[BLOCK_NUM][BLOCK_SIZE];

struct Directory_Entry *dir;
struct Inode *inodes;
uint8_t *inodeMap; // 1 = in use, 0 = empty
uint8_t *blockMap; // 1 = in use, 0 = empty

void Initialize()
{
  dir = (struct Directory_Entry *) &blocks[0];
  inodeMap = (uint8_t*) &blocks[7];
  blockMap = (uint8_t*) &blocks[8];
  // inodes take 128 * 5KB / 8192 ~ 80 blocks
  inodes = (struct Inode *) &blocks[9]; 

  for (int i = 0; i < MAX_FILE_NUM; ++i) { // init entries, inodes and inodeMap
    // entries
    dir[i].valid = 0;
    dir[i].inode = -1;
    memset(&dir[i],0,255);
    // inodes
    inodes[i].attribute = 0;
    inodes[i].size = -1;
    memset(&blocks[0], -1, INODE_BLOCK_NUM);
    // map
    inodeMap[i] = 0;
  }

  for (int i = 0; i < BLOCK_NUM; ++i)
  {
    blockMap[i] = i > 127 ? 0 : 1; // block 0-127 reserved
  }
}

int Df()
{
  int space = 0;
  for (int i = 0; i < BLOCK_NUM; ++i)
  {
    if (blockMap[i] == 0)
    {
      space += BLOCK_SIZE;
    }
  }

  printf("Disk space left: %d Bytes\n", space);
  return space;
}

int Put(FILE* inf)
{
  return 0;
}

struct Directory_Entry *Get(const char* outf)
{
  return 0;
}

// struct Directory_Entry *Get(const char* outf, FILE* dest)
// {
//   return 0;
// }

int Del(const char * fname)
{
  struct Directory_Entry *del = Get(fname);
  if (!del) 
  {
    return -1;
  }
  else 
  {
    del->valid = 0;
    memset(del->name,0,255);
    struct Inode *inp = &inodes[del->inode];
    for (int i = 0; i < INODE_BLOCK_NUM; ++i)
    {
      if (inp->blocks[i] != -1)
      {
        blockMap[ inp->blocks[i] ] = 0;
      }
    }
  }
  return 0;
}

int main()
{
  Initialize();
  printf("%d, %d, %d, %d\n", dir[127].valid, dir[127].inode, blockMap[127], inodeMap[127]);
  printf("%d, %d\n", blockMap[128], blockMap[4226]);
  printf("df return = %d, should return %d\n", Df(), (BLOCK_NUM - 128) * BLOCK_SIZE);
  return 0;
}
