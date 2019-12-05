#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>

// settings about file system
#define BLOCK_NUM 4226          // blocks [0..127] are researved
                                // init num empty blocks = 4098 
#define BLOCK_SIZE 8192
#define MAX_FILE_NUM 128        // determines the number of dir entries and inodes
#define INODE_BLOCK_NUM 1250    // determines the number of blocks each inode can have
#define MAX_FILE_SIZE 10240000  // ~ 10 M

// for parsing command line input
#define WHITESPACE " \t\n"
#define MAX_COMMAND_SIZE 255
#define MAX_NUM_ARGUMENTS 10

// macros to decode / set attribute integer
#define ATTRIBUTE_GET_H(x) ( (x) / 2 )          // hide      = high bit
#define ATTRIBUTE_GET_R(x) ( (x) % 2)           // read-only = low bit
#define PLUSMINUS(x)       ( (x) ? '+' : '-' )

struct Directory_Entry { // Entry ~ 2
  uint8_t  valid;
  char     name[255];
  uint32_t inode;
  time_t   time;
};

struct Inode {                      // Inode ~ 5 KB
  uint8_t  attribute;               // 0: h-r-  1: h-r+  2: h+r-  3: h+r+
  uint32_t blocks[INODE_BLOCK_NUM]; // max file size ~ 10 MB
  uint32_t size;
};

uint8_t blocks[BLOCK_NUM][BLOCK_SIZE];

struct Directory_Entry *dir;
struct Inode *inodes;
uint8_t *inodeMap; // 1 = in use, 0 = empty
uint8_t *blockMap; // 1 = in use, 0 = empty

FILE *image = NULL;

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
    memset(dir[i].name,0,255);
    dir[i].time = 0;
    // inodes
    // inodes[i].valid = 0;
    inodes[i].attribute = 0;
    inodes[i].size = -1;
    memset(&inodes[i].blocks[0], -1, INODE_BLOCK_NUM);
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

  return space;
}

void PrintDf()
{
  int space = Df();
  printf("%d bytes free.\n", space);
}

// search for next empty block and return the index
int GetEmptyBlock()
{  
  for (int i = 128; i < BLOCK_NUM; ++i)
  {
    if (blockMap[i] == 0)
    {
      return i;
    }
  }
  return -1;
}

// search for next empty inode and return the index
int GetEmptyInode() 
{  
  for (int i = 0; i < MAX_FILE_NUM; ++i)
  {
    if (inodeMap[i] == 0)
    {
      return i;
    }
  }
  return -1;
}

// search for next invalid entry and return the index
int GetEmptyDirEntry() 
{  
  for (int i = 0; i < MAX_FILE_NUM; ++i)
  {
    if (dir[i].valid == 0)
    {
      return i;
    }
  }
  return -1;
}

// search for entry index by file name
int GetDir(const char* fname)
{
  for (int i = 0; i < MAX_FILE_NUM; ++i)
  {
    if (strcmp(fname, dir[i].name) == 0)
    {
      return i;
    }
  }
  return -1;
}

// release all blocks under an inode id 
void Erase(int nid)
{
  printf("\nErasing node #%d\n", nid);
  inodes[nid].size = 0;
  int i = 0;
  while (i < INODE_BLOCK_NUM && inodes[nid].blocks[i] != -1)
  {
    blockMap[ inodes[nid].blocks[i] ] = 0;
    inodes[nid].blocks[i] = -1;
  }
}

inline int WritePermission(int nid)
{
  return ! ATTRIBUTE_GET_R( inodes[nid].attribute );
}

// copy file into the file system by fname
// if exists an entry with same name, then overwrite
// else create a new entry
int Put(const char *fname)
{
  if ( strlen(fname) > 32 )
  {
    printf("put error: File name too long.\n");
    return -1;
  }
  
  // Open the input file read-only 
  FILE *ifp = fopen ( fname, "r" ); 
  if (!ifp) // cannot open file
  {
    printf("put error: File does not exist.\n");
    return -1;
  }
  int    status;                   // Hold the status of all return values.
  struct stat buf;                 // stat struct to hold the returns from the stat call
  status =  stat( fname, &buf ); 
  if (status == -1)
  {
    perror("put error: stat");
    return -1;
  }

  int did = GetDir(fname); // directory entry id
  int nid = dir[did].inode;
  int erase = 0;
  // if there's an existing file, keep the entry and inode but erase all it's blocks
  if (did != -1) 
  {
    if ( WritePermission(nid) )
    {
      Erase(nid);
      erase = 1;
    }
    else
    {
      printf("put error: No permission to write file \"%s\"\n", fname);
      return -1;
    }
  }
  // else allocate new entry and inode
  else
  {
    did = GetEmptyDirEntry();
    if (did == -1)
    {
      printf("put error: No more directory entry is allowed.\n");
      return -1;
    }
    else
    {
      nid = GetEmptyInode();
      if (nid == -1)
      {
        printf("put error: No more empty Inode.\n");
        return -1;
      }
      dir[did].inode = nid;
    }
  }

  // Save off the size of the input file since we'll use it in a couple of places
  int copy_size   = buf . st_size;
  if ( copy_size > MAX_FILE_SIZE)
  {
    printf("put error: File size is bigger than max size.\n");
    return -1;
  }
  if ( copy_size > Df() )
  {
    printf("put error: Not enough disk space.\n");
    return -1;
  }

  printf("Reading %d bytes from %s\n", (int) buf . st_size, fname );

  // We want to copy and write in chunks of BLOCK_SIZE. So to do this 
  // we are going to use fseek to move along our file stream in chunks of BLOCK_SIZE.
  // We will copy bytes, increment our file pointer by BLOCK_SIZE and repeat.
  int offset      = 0;               

  // We are going to copy and store our file in BLOCK_SIZE chunks instead of one big 
  // memory pool. Why? We are simulating the way the file system stores file data in
  // blocks of space on the disk. block_index will keep us pointing to the area of
  // the area that we will read from or write to.
  int block_index = GetEmptyBlock();
  if (block_index == -1)
  {
    // this should not happen because of size check
    printf("No more empty blocks found!!!!!!!!!!!\n");
    return -1;
  }
  // copy_size is initialized to the size of the input file so each loop iteration we
  // will copy BLOCK_SIZE bytes from the file then reduce our copy_size counter by
  // BLOCK_SIZE number of bytes. When copy_size is less than or equal to zero we know
  // we have copied all the data from the input file.
  int id = 0;
  while( copy_size > 0 )
  {
    // Index into the input file by offset number of bytes.  Initially offset is set to
    // zero so we copy BLOCK_SIZE number of bytes from the front of the file.  We 
    // then increase the offset by BLOCK_SIZE and continue the process.  This will
    // make us copy from offsets 0, BLOCK_SIZE, 2*BLOCK_SIZE, 3*BLOCK_SIZE, etc.
    fseek( ifp, offset, SEEK_SET );

    // Read BLOCK_SIZE number of bytes from the input file and store them in our
    // data array. 
    int bytes  = fread( blocks[block_index], BLOCK_SIZE, 1, ifp );

    // If bytes == 0 and we haven't reached the end of the file then something is 
    // wrong. If 0 is returned and we also have the EOF flag set then that is OK.
    // It means we've reached the end of our input file.
    if ( bytes == 0 && !feof( ifp ) )
    {
      printf("An error occured reading from the input file.\n");
      return -1;
    }
    else
    {
      blockMap[block_index] = 1;
      inodes[nid].blocks[id] = block_index;
      ++id;
    }
    
    // Clear the EOF file flag.
    clearerr( ifp );

    // Reduce copy_size by the BLOCK_SIZE bytes.
    copy_size -= BLOCK_SIZE;
    
    // Increase the offset into our input file by BLOCK_SIZE.  This will allow
    // the fseek at the top of the loop to position us to the correct spot.
    offset    += BLOCK_SIZE;

    // Increment the index into the block array 
    block_index ++;
  }

  if (!erase) // set up entry & inode if they are new
  {
    dir[did].valid = 1;
    strcpy(dir[did].name, fname);
    inodeMap[ nid ] = 1;
  }
  inodes[ nid ].size = buf.st_size;
  time(&dir[did].time);

  // debug
  printf("Put file: %s (size=%d), entry #%d, node #%d\n", dir[did].name, inodes[nid].size, did, nid);

  // We are done copying from the input file so close it out.
  fclose( ifp );

  return 0;
}

int Get(const char* fname)
{
  int did = GetDir(fname);
  if (did == -1)
  {
    printf("get error: File not found.\f");
    return -1;
  }

  int nid = dir[did].inode;

  FILE *ofp;
  ofp = fopen(fname, "w");

  if( ofp == NULL )
  {
    printf("Could not open output file: %s\n", fname );
    perror("Opening output file returned");
    return -1;
  }

  // Initialize our offsets and pointers just we did above when reading from the file.
  int block_index = 0;
  int copy_size   = inodes[nid].size;
  int offset      = 0;

  printf("Writing %d bytes to %s\n", inodes[nid].size, fname );

  // Using copy_size as a count to determine when we've copied enough bytes to the output file.
  // Each time through the loop, except the last time, we will copy BLOCK_SIZE number of bytes from
  // our stored data to the file fp, then we will increment the offset into the file we are writing to.
  // On the last iteration of the loop, instead of copying BLOCK_SIZE number of bytes we just copy
  // how ever much is remaining ( copy_size % BLOCK_SIZE ).  If we just copied BLOCK_SIZE on the
  // last iteration we'd end up with gibberish at the end of our file. 
  while( copy_size > 0 )
  { 

    int num_bytes;

    // If the remaining number of bytes we need to copy is less than BLOCK_SIZE then
    // only copy the amount that remains. If we copied BLOCK_SIZE number of bytes we'd
    // end up with garbage at the end of the file.
    if( copy_size < BLOCK_SIZE )
    {
      num_bytes = copy_size;
    }
    else 
    {
      num_bytes = BLOCK_SIZE;
    }

    // Write num_bytes number of bytes from our data array into our output file.
    fwrite( blocks[ inodes[nid].blocks[block_index] ], num_bytes, 1, ofp ); 

    // Reduce the amount of bytes remaining to copy, increase the offset into the file
    // and increment the block_index to move us to the next data block.
    copy_size -= num_bytes;
    offset    += BLOCK_SIZE;
    ++block_index;

    // Since we've copied from the point pointed to by our current file pointer, increment
    // offset number of bytes so we will be ready to copy to the next area of our output file.
    fseek( ofp, offset, SEEK_SET );
  }

  // Close the output file, we're done. 
  fclose( ofp );


  return 0;
}

int GetDest(const char* fname, const char* dest)
{
  int did = GetDir(fname);
  if (did == -1)
  {
    printf("get error: File not found.\f");
    return -1;
  }

  int nid = dir[did].inode;

  FILE *ofp;
  ofp = fopen(dest, "w");

  if( ofp == NULL )
  {
    printf("Could not open output file: %s\n", dest );
    perror("Opening output file returned");
    return -1;
  }

  // Initialize our offsets and pointers just we did above when reading from the file.
  int block_index = 0;
  int copy_size   = inodes[nid].size;
  int offset      = 0;

  printf("Writing %d bytes to %s\n", copy_size, dest );

  // Using copy_size as a count to determine when we've copied enough bytes to the output file.
  // Each time through the loop, except the last time, we will copy BLOCK_SIZE number of bytes from
  // our stored data to the file fp, then we will increment the offset into the file we are writing to.
  // On the last iteration of the loop, instead of copying BLOCK_SIZE number of bytes we just copy
  // how ever much is remaining ( copy_size % BLOCK_SIZE ).  If we just copied BLOCK_SIZE on the
  // last iteration we'd end up with gibberish at the end of our file. 
  while( copy_size > 0 )
  { 

    int num_bytes;

    // If the remaining number of bytes we need to copy is less than BLOCK_SIZE then
    // only copy the amount that remains. If we copied BLOCK_SIZE number of bytes we'd
    // end up with garbage at the end of the file.
    if( copy_size < BLOCK_SIZE )
    {
      num_bytes = copy_size;
    }
    else 
    {
      num_bytes = BLOCK_SIZE;
    }

    int bid = inodes[nid].blocks[block_index];
    //printf("bid = %d\n",bid);

    // Write num_bytes number of bytes from our data array into our output file.
    fwrite( blocks[bid], num_bytes, 1, ofp ); 

    // Reduce the amount of bytes remaining to copy, increase the offset into the file
    // and increment the block_index to move us to the next data block.
    copy_size -= num_bytes;
    offset    += num_bytes;
    ++block_index;
    
    // Since we've copied from the point pointed to by our current file pointer, increment
    // offset number of bytes so we will be ready to copy to the next area of our output file.
    fseek( ofp, offset, SEEK_SET );
  }

  // Close the output file, we're done. 
  fclose( ofp );


  return 0;
}

int Del(const char * fname)
{
  int did = GetDir(fname);
  struct Directory_Entry *entry = &dir[did];
  if (did == -1) 
  {
    printf("del error: File not found.\n");
    return -1;
  }
  else 
  {
    if ( WritePermission(dir[did].inode) )
    {
      Erase(entry->inode);
      inodeMap[entry->inode] = 0;
      inodes[entry->inode].size = 0;
      inodes[entry->inode].attribute = 0;
      entry->valid = 0;
      memset(entry->name,0,255);
      entry->inode = -1;
    }
    else
    {
      printf("del error: No permission to delete file \"%s\"\n", fname);
    }
  }
  return 0;
}

int Createfs(const char* fname) 
{
  FILE *ofp;
  ofp = fopen(fname, "w");

  if( ofp == NULL )
  {
    printf("Could not open output file: %s\n", fname );
    perror("Opening output file returned");
    return -1;
  }

  int size = fwrite(blocks[0], BLOCK_SIZE, BLOCK_NUM, ofp);
  if (size != BLOCK_NUM)
  {
    perror("createfs error: Failed to write all blocks.");
    return -1;
  }
  return 0;
}

void PrintDir(int did)
{
  int nid = dir[did].inode;
  char attr[5];
  attr[0] = 'h';
  attr[1] = PLUSMINUS( ATTRIBUTE_GET_H(inodes[nid].attribute) );
  //printf("(%d) converted to (%c)\n", inodes[nid].attribute, attr[1]);
  attr[2] = 'r';
  attr[3] = PLUSMINUS( ATTRIBUTE_GET_R(inodes[nid].attribute) );
  attr[4] = 0;

  printf("%d | %s | %s | %s\n", inodes[nid].size, ctime(&dir[did].time),
                                attr, dir[did].name);
}

int List(int showAll) // if show then print all hidden files
{
  int found = 0;
  for (int i = 0; i < MAX_FILE_NUM; ++i)
  {
    if ( dir[i].valid )
    {
      if (dir[i].inode < 0 || dir[i].inode > 127) // this should not happen
      {
        printf("list error: Illegal inode index(%d) found in file '%s'\n", i, dir[i].name);
        return -1;
      }
      int hidden = ATTRIBUTE_GET_H(inodes[dir[i].inode].attribute);
      if ( showAll || !hidden )
      {
        PrintDir(i);
      }
      if (!found) { found = 1; }
    }
  }

  if (!found)
  {
    printf("list: No files found.\n");
  }

  return 0;
}

int Open(const char *fname)
{
  // Open the input file for read and write
  image = fopen ( fname, "r+" ); 

  int    status;                   // Hold the status of all return values.
  struct stat buf;                 // stat struct to hold the returns from the stat call
  status =  stat( fname , &buf ); 
  if (status == -1)
  {
    perror("open error: File not found.");
    return -1;
  }
  // quick check the file size
  if (buf.st_size != BLOCK_NUM * BLOCK_SIZE)
  {
    printf("open error: Wrong file size.\n");
    return -1;
  }

  if ( fread(&blocks[0], BLOCK_SIZE, BLOCK_NUM, image) == -1)
  {
    printf("open error: Failed to read blocks.\n");
    return -1;
  }
  
  return 0;
}

int Close()
{
  if (image == NULL)
  {
    printf("close error: No opened image file.\n");
    return -1;
  }
  else
  {
    fseek(image, 0, SEEK_SET);
    int size = fwrite(&blocks[0], BLOCK_SIZE, BLOCK_NUM, image);
    if (size != BLOCK_NUM)
    {
      printf("close error: Failed to write all blocks (#%d)\n", size);
      perror("error");
      return -1;
    }
    fclose(image);
    image = NULL;
    Initialize(); // reset the metadata
    return 0;
  }
}

int Attrib(char attr, char sign, const char* fname)
{
  int did = GetDir(fname);
  if (did == -1)
  {
    printf("attrib error: No such file \"%s\"\n",fname);
    return -1;
  }
  int nid = dir[did].inode;
  // printf("inode #%d, attr = %d\n", nid, inodes[nid].attribute);
  if (attr == 'h')
  {
    if (sign == '+') { inodes[nid].attribute ^= 2; }       // attr ^= 0b10
    else if (sign == '-') { inodes[nid].attribute &= 1; }  // attr &= 0b01
  }
  else if (attr == 'r')
  {
    if (sign == '+') { inodes[nid].attribute ^= 1; }
    else if (sign == '-') { inodes[nid].attribute &= 2; }
  }
  
  // printf("inode #%d, attr = %d\n", nid, inodes[nid].attribute);
  return 0;
}

int AttribHelper(const char* str, const char* fname)
{
  // printf("ahelper len = %zu|%s\n", strlen(str), str);
  // printf("(%c)(%c)\n", str[0], str[1]);
  if (strlen(str) != 2 || ( str[1] != 'h' && str[1] != 'r' ) || ( str[0] != '+' && str[0] != '-'))
  {
    printf("attrib error: Wrong command format.\n");
    return -1;
  }
  else
  {
    int ret = Attrib(str[1],str[0], fname);
    return ret;
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// 
// User Input
//
// This function processes user input into command tokens 
int Tokenize(char* str, char** token, int* token_count)
{ 
  char *working_str  = strdup( str );
  // we are going to move the working_str pointer so
  // keep track of its original value so we can deallocate
  // the correct amount at the end
  char *working_root = working_str;                                                  
  
  // saveptr for strtok_r
  char *arg_ptr;  

  // Tokenize the input strings with whitespace used as the delimiter
  // Empty tokens will not be saved in the array
  for ( ; *token_count < MAX_NUM_ARGUMENTS; working_str = NULL, ++(*token_count) )
  {
    char* t = strtok_r(working_str, WHITESPACE, &arg_ptr);
    if ( !t ) { break; }
    snprintf(token[ *token_count ], MAX_COMMAND_SIZE, "%s", t );
  }

  free( working_root );
  return 0;
}

// Check if a char pointed by `ptr` appears in a string `set`
int IsElement(char* ptr, const char* set) 
{
  int i = 0;
  while ( i < strlen(set) )
  {
    if ( *ptr == set[i++] ) { return 1; }
  }
  return 0;
}

// This function trims whitespaces on both ends
// return str pointer passed in
char* TrimWhiteSpace(char* str)
{
  assert(str);
  if ( strlen(str) < 1 ) return str;

  char* temp = strdup(str);
  // end temp string after last non whitespace char
  char* ptr = temp + strlen(str); // point to terminal null char
  while (ptr != temp && IsElement(ptr-1,WHITESPACE))
  {
    --ptr;
  }
  *ptr = 0;
  // move ptr to first non whitespace char
  ptr = temp;
  while ( IsElement(ptr,WHITESPACE) )
  {
    ++ptr;
  }
  // copy
  strcpy(str,ptr);
  free(temp);
  return str;
}

int main()
{
  Initialize();

  // cmd input string
  char* cmd_str = (char*) calloc( MAX_COMMAND_SIZE, sizeof(char) );
  char* working_ptr = cmd_str;

  // For parsing command tokens
  char* token[MAX_NUM_ARGUMENTS];
  for (int i = 0; i < MAX_NUM_ARGUMENTS; ++i)
  {
    token[i] = (char*)calloc(MAX_COMMAND_SIZE, sizeof(char));
  }
  int token_count = 0;

  // main loop
  while (1) 
  {
    printf ("msh> ");
    while( !fgets (cmd_str, MAX_COMMAND_SIZE, stdin) );
    /* Trim whitespace at both ends */
    working_ptr = TrimWhiteSpace(cmd_str);
    if ( !working_ptr || !strlen(working_ptr) )
      continue; // empty str, restart loop

    /* Parse input */
    token_count = 0;
    Tokenize(working_ptr, token, &token_count);

    if ( strcmp("put", token[0]) == 0)
    {
      Put(token[1]);
      continue; // restart loop after printing history
    }

    else if ( strcmp("list", token[0]) == 0)
    {
      if (token_count == 2 && strcmp("-a", token[1]) == 0)
      {
        List(1); // showAll
      }
      else
      {
        List(0);
      }
      continue; // restart loop after printing history
    }
    else if ( strcmp("get", token[0]) == 0)
    {
      if (token_count == 2)
      {
        Get(token[1]);
      }
      else if (token_count == 3)
      {
        GetDest(token[1], token[2]);
      }
      else
      {
        printf("Usage: get filename [destination] (optional)\n");
      }
      continue;
    }

    else if (strcmp("createfs", token[0]) == 0)
    {
      Createfs(token[1]);
      continue;
    }

    else if (strcmp("open", token[0]) == 0)
    {
      Open(token[1]);
      continue;
    }

    else if (strcmp("close", token[0]) == 0)
    {
      Close();
      continue;
    }

    else if (strcmp("del", token[0]) == 0)
    {
      Del(token[1]);
      continue;
    }

    else if (strcmp("attrib", token[0]) == 0)
    {
      if (token_count != 3)
      {
        printf("Usage: attrib command (e.g. h+ or r-) filename\n");
      }
      AttribHelper(token[1], token[2]);
      continue;
    }
    
    else if (strcmp("df", token[0]) == 0)
    {
      PrintDf();
      continue;
    }
    
    else if (strcmp("exit", token[0]) == 0 || strcmp("quit", token[0]) == 0)
    {
      break;
    }

    else
    {
      printf("error: Unknown command.\n");
      continue;
    }
    
  }
  
  // mem recycle
  for (int i = 0; i < MAX_NUM_ARGUMENTS; ++i)
  {
    free(token[i]);
  }
  free(cmd_str);


  return 0;
}
