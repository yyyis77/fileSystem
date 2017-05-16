// Created by Yongyi Yang
// Creation time: 5/15/17.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include "disk.h"

#define MAX_FILE_NUM 8
#define MAX_OPEN_FILE_NUM 4

struct directory_node{
    uint8_t status;
    char file_name[5];
    int length;
    int inode_block;
};

struct inode{
    int data_block_num;
    int data_block_no[64];
};

struct file_descriptor{
    int status;
    int file_offset;
    int directory_node_no;
};

int file_count;
int file_open_count;
struct directory_node* directory_list;
struct inode* inode_list;
struct file_descriptor* oft;
uint8_t bitmap[MAX_FILE_NUM];
uint8_t bytemap[128];


int make_fs(char *disk_name){
    int name_length=0;
    char metadata='1';

    while(disk_name[name_length] != '\0'){
       name_length++;
        if(name_length>4){
            return -1;
        }
    }

    make_disk(disk_name);

    open_disk(disk_name);
    block_write(0,&metadata);
    close_disk();
    return 0;
}

int mount_fs(char *disk_name){
    int i,j;
    char tmp[16];

    open_disk(disk_name);

    block_read(0,tmp);
    if(tmp[0] != '1'){
        perror("did not make file system\n");
        return -1;
    }

    directory_list = malloc(sizeof(struct directory_node) * MAX_FILE_NUM);
    for(i=0; i<MAX_FILE_NUM; i++){
        block_read(i+1,tmp);
        memcpy(&directory_list[i],tmp,sizeof(struct directory_node));
    }

    block_read(10,tmp);
    memcpy(bitmap, tmp, sizeof(bitmap));
    int offset=64;
    for(i=0; i<MAX_FILE_NUM; i++){
        bitmap_to_bytemap(bitmap[i],&bytemap[i+offset]);
        offset+=8;
    }

    offset=11;
    char tmp64[64];
    inode_list = malloc(sizeof(struct inode)*MAX_FILE_NUM);
    for(i=0; i<MAX_FILE_NUM; i++){
        for(j=0; j<4; j++) {
            block_read(i+offset, &tmp64[j*16]);
        }

        inode_list[i].data_block_num=0;
        for(j=0; j<64; j++){
            if(tmp64[j] == 0){
                break;
            }
            inode_list[i].data_block_no[inode_list[i].data_block_num] = tmp64[j];
            inode_list[i].data_block_num++;
        }
    }

    for(i=0; i<MAX_FILE_NUM; i++){
        if(directory_list[i].status==1){
            file_count++;
        }
    }

    oft = malloc(sizeof(struct file_descriptor)*MAX_OPEN_FILE_NUM);
    file_open_count=0;

    return 0;
}

int dismount_fs(char *disk_name){
    int i,j,offset=11;
    uint8_t tmp[16], tmp64[64];
    for(i=0; i<MAX_OPEN_FILE_NUM; i++){
        oft[i].status=0;
    }
    // write back directory
    for(i=0; i<MAX_FILE_NUM; i++){
        if(directory_list[i].status==1){
            memset(tmp,0,16);
            memcpy(tmp,&directory_list[i], sizeof(struct directory_node)*MAX_FILE_NUM);
            block_write(i+1,tmp);
        }
    }
    // write back bitmap
    bytemap_to_bitmap(bytemap,bitmap);
    memcpy(tmp,bitmap, sizeof(uint8_t)*MAX_FILE_NUM);
    block_write(10,tmp);
    // write back inode
    for(i=0; i<MAX_FILE_NUM; i++){
        memset(tmp64,0,64);
        for (j = 0; j < inode_list[i].data_block_num; j++) {
            tmp64[inode_list[i].data_block_no[j]]=1;
        }
        for(j=0; j<4; j++){
            memcpy(tmp,&tmp64[j*16],16);
            block_write(offset+j,tmp);
        }
    }
    close_disk();
}

int fs_create(char *name){
    int i,name_length=0;
    int free_dir=0;
    while(name[name_length] != '\0'){
        name_length++;
    }
    if(name_length>4){
        perror("Cannot create file, file name too long\n");
        return -1;
    }
    if(file_count>=8){
        perror("Cannot create file, file more than 8\n");
        return -1;
    }
    for(i=0; i<MAX_FILE_NUM; i++){
        if(directory_list[i].status==1 && strcmp(directory_list[i].file_name,name)==0){
            perror("Cannot create file,,file exists\n");
            return -1;
        }
    }
    // get first empty directory node number and initiliaze
    for(i=0; i<MAX_FILE_NUM; i++){
        if(directory_list[i].status==0){
            free_dir=i;
            break;
        }
    }
    directory_list[free_dir].status=1;
    strcpy(directory_list[free_dir].file_name,name);
    directory_list[free_dir].length=0;
    directory_list[free_dir].inode_block=11+4*free_dir;

    // clear the inode blocks
    char *tmp=malloc(sizeof(char)*16);
    memset(tmp,0,16);
    for(i=0; i<4; i++){
        block_write(directory_list[free_dir].inode_block+i,tmp);
    }

    file_count++;
    return 0;
}

int fs_delete(char *name){
    int i,cur_dir_num=-1;
    for(i=0; i<MAX_FILE_NUM; i++){
        if(strcmp(directory_list[i].file_name,name)==0){
            cur_dir_num=i;
            break;
        }
    }
    if(cur_dir_num==-1){
        perror("Cannot delete file, it doesn't exist\n");
        return -1;
    }
    for(i=0; i<MAX_OPEN_FILE_NUM; i++){
        if(oft[i].status==1 && oft[i].directory_node_no==cur_dir_num){
            perror("Cannot delete file, it's openning\n");
            return -1;
        }
    }
    // clear directory list
    directory_list[cur_dir_num].status=0;
    // clear data bytemap
    for(i=0; i<inode_list[cur_dir_num].data_block_num; i++){
        bytemap[inode_list[cur_dir_num].data_block_no[i]]=0;
    }
    // clear inode list
    inode_list[cur_dir_num].data_block_num=0;
    memset(inode_list[cur_dir_num].data_block_no,0,64* sizeof(int));

    file_count--;
    return 0;
}

int fs_open(char *name){
    int i,cur_dir_num=-1;
    if(file_open_count>=4){
        perror("Cannot open file, maximum open file count\n");
        return -1;
    }
    for(i=0; i<MAX_FILE_NUM; i++){
        if(strcmp(directory_list[i].file_name,name)==0){
            cur_dir_num=i;
            break;
        }
    }
    if(cur_dir_num==-1){
        perror("Cannot open file, it doesn't exist\n");
        return -1;
    }
    for(i=0; i<MAX_OPEN_FILE_NUM; i++){
        if(oft[i].status==1 && oft[i].directory_node_no==cur_dir_num){
            perror("Cannot open file, it's openning\n");
            return -1;
        }
    }
    for(i=0; i<4; i++){
        if(oft[i].status==0){
            oft[i].status=1;
            oft[i].directory_node_no=cur_dir_num;
            oft[i].file_offset=0;
            file_open_count++;
            return i;
        }
    }
}

int fs_close(int fildes){
    int i,j;
    char tmp[16];
    char tmp64[64];

    if(fildes>=4 || oft[fildes].status==0){
        perror("Cannot close file, fildes doesn't exist\n");
        return -1;
    }
    oft[fildes].status=0;
    // write back directory
    int cur_directory_no=oft[fildes].directory_node_no;
    memset(tmp,0,16);
    memcpy(tmp,&directory_list[cur_directory_no], sizeof(struct directory_node));
    block_write(cur_directory_no,tmp);

    // write back data bitmap
    int offset = 64;
    for(i=0; i<MAX_FILE_NUM; i++){
        bytemap_to_bitmap(&bytemap[i*8+offset],bitmap[i]);
    }
    memset(tmp,0,16);
    memcpy(tmp,bitmap, sizeof(uint8_t)*MAX_FILE_NUM);
    block_write(10,tmp);

    // write back inode
    offset=11;
    memset(tmp64,0,64);
    for (j = 0; j < inode_list[i].data_block_num; j++) {
        tmp64[inode_list[i].data_block_no[j]]=1;
    }
    for(i=0; i<4; i++){
        memcpy(tmp,&tmp64[i*16],16);
        block_write(offset+i,tmp);
    }

    return 0;
}

int fs_read(int fildes, void *buf, size_t nbyte){
    int dir_no, cur_offset, cur_bytes=0;
    int file_len,block_id;
    uint8_t tmp[16];
    if(oft[fildes].status==0){
        perror("Cannot read file, fildes not valid\n");
        return -1;
    }
    dir_no=oft[fildes].directory_node_no;
    file_len=directory_list[dir_no].length;
    if(oft[fildes].file_offset+nbyte>=file_len){
        nbyte=file_len-oft[fildes].file_offset;
    }
    block_id=oft[fildes].file_offset/16;
    cur_offset=oft[fildes].file_offset%16;

    while(cur_bytes<nbyte){
        if(cur_offset!=0) {
            block_read(inode_list[dir_no].data_block_no[block_id], (char *)tmp);
            cur_bytes = 16 - cur_offset;
            if(nbyte>cur_bytes) {
                memcpy(buf, tmp+cur_offset, cur_bytes);
            }
            else{
                memcpy(buf,tmp+cur_offset,nbyte);
            }
            buf += cur_bytes;
            cur_offset = 0;
            block_id++;
        }
        else if(cur_bytes+16<=nbyte){
            block_read(inode_list[dir_no].data_block_no[block_id],(char*)tmp);
            memcpy(buf,tmp,16);
            buf += 16;
            block_id++;
            cur_bytes+=16;
        }
        else{
            block_read(inode_list[dir_no].data_block_no[block_id],(char*)tmp);
            cur_bytes=(int)nbyte-cur_bytes;
            memcpy(buf,tmp,cur_bytes);
            cur_bytes=(int)nbyte;
        }
    }
    oft[dir_no].file_offset+=nbyte;
    return (int)nbyte;
}

int fs_write(int fildes, void *buf, size_t nbyte){
    int i,dir_no, cur_offset, free_block_num=0;
    int cur_bytes=0;
    int file_len,block_id;
    int enlarge_block_num;
    uint8_t tmp[16];

    if(oft[fildes].status==0){
        perror("Cannot write file, fildes not valid\n");
        return -1;
    }

    block_id=oft[fildes].file_offset/16;
    cur_offset=oft[fildes].file_offset%16;

    dir_no=oft[fildes].directory_node_no;
    for(i=0; i<64; i++){
        if(bytemap[i]==0){
            free_block_num++;
        }
    }
    if(oft[fildes].file_offset+nbyte>(free_block_num+inode_list[dir_no].data_block_num-block_id)*16){
        nbyte=(free_block_num+inode_list[dir_no].data_block_num-block_id)*16-oft[fildes].file_offset;
    }

    if(oft[fildes].file_offset+nbyte>inode_list[dir_no].data_block_num*16){
        enlarge_block_num=(oft[fildes].file_offset+nbyte)/16-inode_list[dir_no].data_block_num;
        for(i=inode_list[dir_no].data_block_num; i<inode_list[dir_no].data_block_num+enlarge_block_num; i++){
            inode_list[dir_no].data_block_no[i]=get_new_data_block();
        }
    }
    // update file length in the directory
    if(oft[fildes].file_offset+nbyte>directory_list[dir_no].length){
        directory_list[dir_no].length=oft[fildes].file_offset+nbyte;
    }
    while(cur_bytes<nbyte){
        if(cur_offset!=0){
            block_read(inode_list[dir_no].data_block_no[block_id], (char*)tmp);
            cur_bytes = 16 - cur_offset;
            if(cur_bytes > nbyte){
                memcpy(tmp+cur_offset,buf,cur_bytes);
            }
            else{
                memcpy(tmp+cur_offset,buf,nbyte);
            }
            block_write(inode_list[dir_no].data_block_no[block_id],(char*)tmp);
            cur_offset=0;
            buf+=cur_bytes;
            block_id++;
        }
        else if(cur_bytes+16<=nbyte){
            block_write(inode_list[dir_no].data_block_no[block_id],(char*)buf);
            buf+=16;
            block_id++;
            cur_bytes+=16;
        }
        else{
            block_read(inode_list[dir_no].data_block_no[block_id], (char*)tmp);
            cur_bytes=(int)nbyte-cur_bytes;
            memcpy(tmp,buf,cur_bytes);
            block_write(inode_list[dir_no].data_block_no[block_id],(char*)tmp);
            cur_bytes=(int)nbyte;
        }
    }
    return (int)nbyte;
}

int fs_get_filesize(int fildes){
    if(oft[fildes].status==0){
        perror("Cannot get file size, fildes not valid\n");
        return -1;
    }

    int dir_no=oft[fildes].directory_node_no;
    return directory_list[dir_no].length;
}

int fs_lseek(int fildes, off_t offset){
    int dir_no;
    if(oft[fildes].status==0){
        perror("Cannot lseek file, fildes not valid\n");
        return -1;
    }

    dir_no=oft[fildes].directory_node_no;
    if(oft[fildes].file_offset+offset>directory_list[dir_no].length || oft[fildes].file_offset+offset<0){
        perror("Cannot lseek file, offset out of bounds\n");
        return -1;
    }

    oft[fildes].file_offset+=offset;
    return 0;
}

int fs_truncate(int fildes, off_t length){
    int i,dir_no,new_block_num;
    if(oft[fildes].status==0){
        perror("Cannot truncate file, fildes not valid\n");
        return -1;
    }

    dir_no=oft[fildes].directory_node_no;
    if(length>directory_list[dir_no].length){
        perror("length larger than file size\n");
        return -1;
    }

    directory_list[dir_no].length-=length;
    oft[fildes].file_offset=0;
    new_block_num=(directory_list[dir_no].length-length)/16;
    if(new_block_num<inode_list[dir_no].data_block_num){
        for(i=inode_list[dir_no].data_block_num; i>new_block_num; i--){
            bytemap[inode_list[dir_no].data_block_no[i-1]]=0;
            inode_list[dir_no].data_block_no[i-1]=0;
        }
        //length=new_block_num*16-directory_list[dir_no].length+length;
    }
    return 0;
}

int get_new_data_block(){
    int i=64;
    while(i<128){
        if(bytemap[i] == 0){
            bytemap[i]=1;
            return i;
        }
        i++;
    }
    return -1;
}

void bitmap_to_bytemap(uint8_t bitmap, uint8_t bytemap[8]){
    if(bitmap&0x80){
        bytemap[0]=1;
    }
    if(bitmap&0x40){
        bytemap[1]=1;
    }
    if(bitmap&0x20){
        bytemap[2]=1;
    }
    if(bitmap&0x10){
        bytemap[3]=1;
    }
    if(bitmap&0x08){
        bytemap[4]=1;
    }
    if(bitmap&0x04){
        bytemap[5]=1;
    }
    if(bitmap&0x02){
        bytemap[6]=1;
    }
    if(bitmap&0x01){
        bytemap[7]=1;
    }
}

void bytemap_to_bitmap(uint8_t bytemap[8], uint8_t bitmap){
    if(bytemap[0]==1){
        bitmap |= 0x80;
    }
    if(bytemap[1]==1){
        bitmap |= 0x40;
    }
    if(bytemap[2]==1){
        bitmap |= 0x20;
    }
    if(bytemap[3]==1){
        bitmap |= 0x10;
    }
    if(bytemap[4]==1){
        bitmap |= 0x08;
    }
    if(bytemap[5]==1){
        bitmap |= 0x04;
    }
    if(bytemap[6]==1){
        bitmap |= 0x02;
    }
    if(bytemap[7]==1){
        bitmap |= 0x01;
    }
}
