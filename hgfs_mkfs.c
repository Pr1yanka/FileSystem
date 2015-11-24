#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<errno.h>
#include <fcntl.h>
#include "hgfs.h"

#define INODE_BLK_STARTS_AT 3
#define DATA_BLK_STARTS_AT 11
#define SUP_BLK_STARTS_AT 1
#define EMPTY 0

// Data structure definitions






struct  SupBlock;
struct INode;

int MkFS(int dev, int ninodes, int nrootdir, int blksize);
int my_memcpy (const void* dest,const void* src, int size);
int ReadInode(int dev, int ino, struct INode *inode);
int WriteInode(int dev, int ino, struct INode *inode);
int readSuper (int dev, struct SupBlock* sb);
int writeSuper (int dev,struct SupBlock* sb);
int ReadBlock(int dev, int blk, char buf[BLKSIZE]);
int WriteBlock(int dev, int blk,char buf[BLKSIZE]);
int OpenDevice(int dev);
int ShutdownDevice(int dev);

void ls(int dfd,int dev);












//============= TESTING APPLICATION USING THESE FS CALLS ==============

// Menu driven testing application for creation of fs,
// and all file and directory related operations

int dev=0;

int main()
{

    dev=open("disk_image",0666);
	perror ("open()");
	MkFS(dev,INODETABSIZE,ROOTDIRSIZE,8192);
	char buf [2*512];
	struct SupBlock *s= (struct SupBlock*)buf;
	readSuper(dev,s);
	printf("magic=%d\n",s->sb_magic);
    return 0;

}



//============= SYSTEM CALL LEVEL NOT FOLLOWED =======


//============= VNODE/VFS NOT FOLLOWED ===============

//============== UFS INTERFACE LAYER ==========================
struct SupBlock sb;

int MkFS(int dev, int ninodes, int nrootdir, int blksize)
{
	int rootentrycount = 1; //ROOTDIRSIZE;
	int inodecount = ninodes;// INODETABSIZE;
	int blkcount = blksize;//8192;
	char buf[BLKSIZE];
	bzero(buf,BLKSIZE);
	char firstfile_body[]="Coding is not only duty it's beauty too\n";
	int reservblks = 1 + 2 + 8 + 4; // boot, super, inodetable, rootdir blks

	// Assuming only predefined sizes are used
	// Otherwise we have to recompute according to the parameters given.
	//

	// Boot block dummy block (Because no boot loader nothing...)
	bzero(buf, BLKSIZE);
	my_memcpy(buf,"hello",5);
	WriteBlock(dev,0,buf);
	//write(devfd[dev], buf, 512);

	// Write initialized superblock
	strcpy(sb.sb_vname, "HGFS-----");
	sb.sb_magic= HGFS_MAGIC;
	sb.sb_nino = inodecount;
	sb.sb_nblk = blkcount;
	sb.sb_nrootdir = rootentrycount;
	sb.sb_nfreeblk = blkcount - reservblks;
	sb.sb_nfreeino = inodecount;
	sb.sb_flags = 0;//FS_CLEAN;
	sb.	sb_freeblkindex = 0;//(BLKSIZE/sizeof(unsigned short))-1;
	sb.	sb_freeinoindex = 0;//(BLKSIZE - (54))/2 - 1;
	sb.sb_chktime = 0;
	sb.sb_ctime = 0;

	int i;
	for (i=0;i<(BLKSIZE-54)/2;i++) {
		sb.sb_freeinos[i]= i+3;//counting of inodes starts from 1 (not 0),root directory inode no.=1
	}

	for (i=1;i<BLKSIZE/sizeof(unsigned short);i++)
		sb.sb_freeblks[i]=i+4;// counting starts from 0 and blkno. 0,1,2,3 are alloted to root directory

    int next_freeblk_list=i+4;
    //assignning pointer to free block containing next free blk list
    sb.sb_freeblks[0]=next_freeblk_list;
	//write(devfd[dev],&sb,sizeof(sb));
	writeSuper (dev,&sb);



	// Write initialized list of inodes

    struct INode* inode_ptr=(struct INode*) buf;

    for (i=0;i<BLKSIZE/sizeof(struct INode);i++) {
        inode_ptr->i_lnk=0;
        inode_ptr++;
    }
    printf("hiii\n");
    inode_ptr=(struct INode*) buf;

    //alloting first Inode for root directory file
    inode_ptr->i_mode= S_IFDIR ;//specifies that it is a directory
    inode_ptr->i_size=sizeof (struct OnDiskDirEntry);
    inode_ptr->i_gid=255;
    inode_ptr->i_uid=254;
    inode_ptr->i_lnk=1; //root directory hardlinks =1
    inode_ptr->i_gen=1;
    inode_ptr->i_blks[0]=0;
    inode_ptr->i_blks[1]=1;
    inode_ptr->i_blks[2]=2;
    inode_ptr->i_blks[3]=3;
    //only 4 blks are allocated
    
	inode_ptr++;
	//alloting second Inode for welcome file
    inode_ptr->i_mode=(short) S_IFREG ;//specifies that it is a regular file
    inode_ptr->i_size=sizeof(firstfile_body);
    inode_ptr->i_gid=255;
    inode_ptr->i_uid=254;
    inode_ptr->i_lnk=1; // hardlinks =1
    inode_ptr->i_gen=1;
    inode_ptr->i_blks[0]=4;

    WriteBlock(dev,INODE_BLK_STARTS_AT,buf);

    //REST 7 BLKS OF INODE BLOCKS ARE EMPTY
    bzero(buf,BLKSIZE);
    inode_ptr=(struct INode*) buf;
    inode_ptr->i_lnk=0;
    //write 7 empty inode blocks
    for(i=1;i<=7;i++)
        WriteBlock(dev,INODE_BLK_STARTS_AT+i,buf);

    printf("hello\n");
	// Write initialized list of directory entries

    struct OnDiskDirEntry* dirptr=(struct OnDiskDirEntry*)buf;
    dirptr->d_ino=2;
    strcpy(dirptr->d_name,"welcome");
    dirptr++;
//    dirptr->d_ino=1;
  //  strcpy(dirptr->d_name,"..\0");// root and its parent have inode no 1
   // dirptr++;
    for (i=2;i<=BLKSIZE/sizeof(struct OnDiskDirEntry);i++) {
        dirptr ->d_ino=0; //rest entries are currently empty
        dirptr++;
    }
    printf("world\n");
    WriteBlock (dev,DATA_BLK_STARTS_AT+0,buf);

	dirptr= (struct OnDiskDirEntry*) buf;
	dirptr->d_ino=0;

    //write rest 3 directory blocks(empty)

    bzero(buf,BLKSIZE);
	for (i=1;i<4;i++)
        WriteBlock(dev,DATA_BLK_STARTS_AT+i,buf);

	//write first file body
	strcpy(buf,firstfile_body);
	WriteBlock(dev,DATA_BLK_STARTS_AT+4,buf);
	// Fill the remaining empty datablocks

    bzero(buf, BLKSIZE);
    unsigned short* ptr=(short*)buf;
    printf("helloween\n");
    for (i=DATA_BLK_STARTS_AT+5;i<blkcount;i++) {

        WriteBlock(dev,i, buf);
    }

    return 0;

	// Write free blk information (data structures)
}



//utility function
int my_memcpy (const void* dest,const void* src, int size) {
    char* d=(char* ) dest;
    char* s=(char* ) src;
    int i;
    for (i=0;i<size;i++) {
        *d=*s;
        d++;s++;
    }
    return i;
}




//============== UFS INTERNAL LOW LEVEL ALGORITHMS =============




int readSuper (int dev, struct SupBlock* sb) {

    if (sb==NULL)
        return -1;//error
    char* ptr = (char*) sb;
    ReadBlock(dev,SUP_BLK_STARTS_AT+0,ptr);
    ReadBlock(dev,SUP_BLK_STARTS_AT+1,ptr+BLKSIZE);
    return 0;//success
}




int writeSuper (int dev,struct SupBlock* sb) {

    if (sb==NULL)
        return -1;// error
    char* ptr=(char*)sb;
    WriteBlock( dev,SUP_BLK_STARTS_AT+0,ptr);
    WriteBlock(dev,SUP_BLK_STARTS_AT+1,ptr+BLKSIZE);
    return 0;
}






//============== DEVICE DRIVER LEVEL =====================



// Reading a logical block blk from device dev

int ReadBlock(int dev, int blk, char buf[BLKSIZE])
{
	// Check for validity of the block
        if (blk<0||blk > 8192)
            return -1;
	// Check for validity of the device
        if (dev < 0 || dev<0)
            return -1;
	// If OK read the block
	lseek(dev, blk * BLKSIZE, SEEK_SET);
	return read(dev, buf, BLKSIZE);
}



// Writing a logical block blk to device dev
int WriteBlock(int dev, int blk,char buf[BLKSIZE])
{
	// Check for validity of the block
        if (blk<0||blk > 8192)
            return -1;
	// Check for validity of the device
        if (dev < 0 || dev<0)
            return -1;

	// If OK write the block
	lseek(dev, blk * BLKSIZE, SEEK_SET);
	return write(dev, buf, BLKSIZE);

}


