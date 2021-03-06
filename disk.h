#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <memory.h>


/***************************************************************************/
#define DISK_BLOCKS 128       /* number of blocks on the disk              */
#define BLOCK_SIZE 16         /* block size on "disk"                      */              
/***************************************************************************/
int make_disk(char *name);    /* create an empty, virtual disk file        */    
int open_disk(char *name);    /* open a virtual disk (file)                */               
int close_disk();             /* close a previously opened disk (file)     */   

int block_write(int block, char *buf);
                              /* write a block of size BLOCK_SIZE to disk  */ 
int block_read(int block, char *buf);
                              /* read a block of size BLOCK_SIZE from disk */ 
/***************************************************************************/

void bitmap_to_bytemap(uint8_t bitmap, uint8_t bytemap[8]);
void bytemap_to_bitmap(uint8_t bytemap[8], uint8_t bitmap);
int get_new_data_block();
