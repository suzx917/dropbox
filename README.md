# dropbox
A block-based file system.

## Storage
Supports up to 128 files (single level directory) with ~33 MB storage space. Max single file size 10 MB.

## Command
+ `put filename`

  import file
  
+ `get filename [destination]`
  
  export file
  
+ `list`
  
  print file list
  
+ `df`
  
  print size of free space

+ `createfs`

  export image file

+ `open filename`

  open image file
  
+ `close`
  
  save and close image

+ `attrib +h filename`

  make file hidden (-h to remove hidden attribute, +r/-r for read-only lable)
 
  
