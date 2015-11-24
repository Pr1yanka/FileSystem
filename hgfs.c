#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/version.h>
#include "hgfs.h"

static struct kmem_cache *hgfs_inode_cachep;

struct Inode *hgfs_get_inode(struct super_block *sb,
					  int inode_no);

struct dentry *hgfs_lookup(struct inode *parent_inode,
			       struct dentry *child_dentry, unsigned int flags);




void* my_sb_bread(struct super_block *sb,int disk_blkno,struct buffer_head **bh) 
{

	int ker_blkno= (disk_blkno)/8;

	int offset=disk_blkno%8;
	
	*bh= sb_bread(sb,ker_blkno);
	char* ptr= (char*) (*bh)->b_data;
	ptr=ptr + offset * BLKSIZE;
	return ptr;
}


static int hgfs_iterate(struct file *filp, struct dir_context *ctx);

ssize_t hgfs_read(struct file * filp, char __user * buf, size_t len, loff_t * ppos);

ssize_t hgfs_write(struct file * filp, const char __user * buf, size_t len, loff_t * ppos);

const struct file_operations hgfs_file_operations = {
	.read = hgfs_read,
	.write = hgfs_write,
};

const struct file_operations hgfs_dir_operations = {
	.owner = THIS_MODULE,
//#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
	.iterate = hgfs_iterate,
//#else
//	.readdir = simplefs_readdir,
//#endif
};

static struct inode_operations hgfs_inode_ops = {
//	.create = hgfs_create,
	.lookup = hgfs_lookup,
//	.mkdir = hgfs_mkdir,
};

ssize_t hgfs_read(struct file * filp, char __user * buf, size_t len,
		      loff_t * ppos)
{	
	printk(KERN_INFO "read called\n");

/*	------------- After the commit dd37978c5 in the upstream linux kernel,
	 * we can use just filp->f_inode instead of the
	 * f->f_path.dentry->d_inode redirection ------------------*/
	struct INode *inode =
	    HGFS_INODE(filp->f_path.dentry->d_inode);
	struct buffer_head *bh;

	char *buffer;
	int nbytes;

	if (*ppos >= inode->i_size) {
		/* Read request with offset beyond the filesize --------------*/
		return 0;
	}

	buffer =(char*) my_sb_bread(filp->f_path.dentry->d_inode->i_sb,
					    inode->i_blks[0]+DATA_BLK_STARTS_AT,&bh);


	nbytes = min((size_t) inode->i_size, len);
	printk(KERN_INFO "nbytes=%d\n",nbytes);
	if (copy_to_user(buf, buffer, nbytes)) {
		brelse(bh);
		printk(KERN_INFO
		       "Error copying file contents to the userspace buffer\n");
		return -EFAULT;
	}

	brelse(bh);

	*ppos += nbytes;

	return nbytes;
}



int hgfs_inode_save(struct super_block *sb, struct INode *hgfs_inode, int ino)
{
	struct INode *inode_iterator;
	struct buffer_head *bh;

	struct INode* inode_data =(struct INode*) my_sb_bread(sb, INODE_BLK_STARTS_AT,&bh);

/*	if (mutex_lock_interruptible(&simplefs_sb_lock)) {
		sfs_trace("Failed to acquire mutex lock\n");
		return -EINTR;
	}
*/
	inode_iterator=inode_data + ino-1; 

	if (likely(inode_iterator)) {
		memcpy(inode_iterator, hgfs_inode, sizeof(*inode_iterator));
		printk(KERN_INFO "The inode updated\n");

		mark_buffer_dirty(bh);
		sync_dirty_buffer(bh);
	} 
	else {
		printk(KERN_ERR
		       "The new filesize could not be stored to the inode.");
		return -EIO;
	}

	brelse(bh);


	return 0;
}




ssize_t hgfs_write(struct file * filp, const char __user * buf, size_t len,
		       loff_t * ppos)
{
	struct inode *inode;
	struct INode *hgfs_inode;
	struct buffer_head *bh;
	struct super_block *sb;

	char *buffer;

	int retval;

	retval = generic_write_checks(filp, ppos, &len, 0);
	if (retval) {
		return retval;
	}

	inode = filp->f_path.dentry->d_inode;
	hgfs_inode = HGFS_INODE(inode);
	sb = inode->i_sb;

	buffer =(char*) my_sb_bread(sb,hgfs_inode->i_blks[0]+DATA_BLK_STARTS_AT,&bh);

	if (!bh) {
		printk(KERN_ERR "Reading the block number [%llu] failed.",
		       hgfs_inode->i_blks[0]+DATA_BLK_STARTS_AT);
		return 0;
	}

	/* Move the pointer until the required byte offset -------------------------*/
	buffer += *ppos;

	if (copy_from_user(buffer, buf, len)) {
		brelse(bh);
		printk(KERN_ERR
		       "Error copying file contents from the userspace buffer to the kernel space\n");
		return -EFAULT;
	}
	*ppos += len;

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	
	
	
	hgfs_inode->i_size = *ppos;
	retval = hgfs_inode_save(sb, hgfs_inode,inode->i_ino);
	if (retval) {
		len = retval;
	}
//	mutex_unlock(&simplefs_inodes_mgmt_lock);

	return len;
}




static int hgfs_iterate(struct file *filp, struct dir_context *ctx)
{
	printk(	KERN_INFO "hgfs:in iterate method\n");

	loff_t pos;
	struct inode *inode;
	struct super_block *sb;
	struct buffer_head *bh;
	struct INode *hgfs_inode;
	struct OnDiskDirEntry *record;
	int i;

	pos = ctx->pos;
	
	inode = filp->f_dentry->d_inode;
	sb = inode->i_sb;

	if (pos) {
		/* FIXME: We use a hack of reading pos to figure if we have filled in all data.
		 * We should probably fix this to work in a cursor based model and
		 * use the tokens correctly to not fill too many data in each cursor based call */
		return 0;
	}

	hgfs_inode = HGFS_INODE(inode);

/*	if (unlikely(!S_ISDIR(hgfs_inode->i_mode))) {
		printk(KERN_ERR
		       "inode [][%lu] for fs object [%s] not a directory\n",
		        inode->i_ino,
		       filp->f_dentry->d_name.name);
		return -ENOTDIR;
	}
*/
//	bh = sb_bread(sb, sfs_inode->data_block_number);

	record = (struct OnDiskDirEntry *) my_sb_bread(sb,DATA_BLK_STARTS_AT+hgfs_inode->i_blks[0],&bh);
	int d_count=hgfs_inode->i_size/sizeof(struct OnDiskDirEntry);
	for (i = 0; i < d_count; i++) {
		
		dir_emit(ctx, record->d_name, MAXFNAME,
			record->d_ino, DT_UNKNOWN);
		ctx->pos += sizeof(struct OnDiskDirEntry);
		
		pos += sizeof(struct OnDiskDirEntry);
		record++;
	}
	brelse(bh);

	return 0;

}



struct dentry *hgfs_lookup(struct inode *parent_inode,
			       struct dentry *child_dentry, unsigned int flags)
{
	printk(KERN_INFO "in hgfs lookup\n");
	struct INode* hgfs_parent_inode=HGFS_INODE(parent_inode);
	struct super_block* sb= parent_inode->i_sb;
	struct buffer_head *bh;

    int done=0, i=0;

    int data_blks_allocated_to_dir= 1;//hgfs_parent_inode->i_size/BLKSIZE + (hgfs_parent_inode->i_size % BLKSIZE)==0?0:1 ;
    struct OnDiskDirEntry* dp;
	int d_entry_count=hgfs_parent_inode->i_size/sizeof(struct OnDiskDirEntry);
	int k=0;
    while (done==0&&i<data_blks_allocated_to_dir) // assuming dirctory-file is small(no need to look in indirect,double indirect.)
    {	printk(KERN_INFO "in lookup 1st while\n");
        int blkno=hgfs_parent_inode->i_blks[i];
        dp= (struct OnDiskDirEntry*) my_sb_bread (sb,DATA_BLK_STARTS_AT+blkno,&bh);
        int j=0;
        while (done==0 && k<d_entry_count && j<BLKSIZE/sizeof (struct OnDiskDirEntry)) {
            printk(KERN_INFO "in lookup nested while\n");
			if (strcmp (dp->d_name,child_dentry->d_name.name)==0) {
                done = 1;
                printk(KERN_INFO "found\n");


				struct inode *inode;
				struct INode *hgfs_inode;

				hgfs_inode = hgfs_get_inode(sb, dp->d_ino);

				inode = new_inode(sb);
				inode->i_ino = dp->d_ino;
				inode_init_owner(inode, parent_inode, hgfs_inode->i_mode);
				inode->i_sb = sb;
				inode->i_op = &hgfs_inode_ops;

				if (S_ISDIR(inode->i_mode))
					inode->i_fop = &hgfs_dir_operations;
				else if (S_ISREG(inode->i_mode))
					inode->i_fop = &hgfs_file_operations;
				else
					printk(KERN_ERR
						   "Unknown inode type. Neither a directory nor a file");

//				inode->i_atime =hgfs_inode->i_atime;
//				inode->i_mtime = hgfs_inode->i_mtime;
//				inode->i_ctime = hgfs_inode->i_ctime;

				inode->i_private = hgfs_inode;

				d_add(child_dentry, inode);
				return NULL;


               
            }
            j++;dp++;k++;
        }
        i++;
		brelse(bh);
    }
	printk(KERN_ERR
	       "No inode found for the filename [%s]\n",
	       child_dentry->d_name.name);

	return NULL;

}
struct Inode *hgfs_get_inode(struct super_block *sb,
					  int inode_no)
{
	printk(KERN_INFO "hgfs_get_inode called\n");
	struct SupBlocks *hgfs_sb = HGFS_SB(sb);
	struct INode *hgfs_inode = NULL;
	struct INode *inode_buffer = NULL;

	struct buffer_head *bh;


	//* Assuming out of 8 blocks only first block has allocated till now


//	int blkno=0;
   // bh = sb_bread(sb,KERNEL_BLKNO(INODE_BLK_STARTS_AT+blkno));
    hgfs_inode= (struct INode*) my_sb_bread(sb,INODE_BLK_STARTS_AT,&bh);
	hgfs_inode += inode_no -1;
	inode_buffer = kmem_cache_alloc(hgfs_inode_cachep, GFP_KERNEL);
	memcpy(inode_buffer, hgfs_inode, sizeof(*inode_buffer));

	brelse(bh);
	return inode_buffer;
}




void hgfs_destroy_inode (struct inode* ino) {
	struct INode* hgfs_inode=HGFS_INODE(ino);
	printk(KERN_INFO "freeing private data of inode %p or %lu \n"
			,hgfs_inode,ino->i_ino);
	kmem_cache_free(hgfs_inode_cachep,hgfs_inode);
}

static const struct super_operations hgfs_sops = {
	//.alloc_inode=hgfs_alloc_inode,
	.destroy_inode=hgfs_destroy_inode,
};

int hgfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root_inode;
	struct buffer_head *bh;
	struct SupBlock *sb_disk;
	int ret = -EPERM;
//	sb->s_blocksize=512;
	printk(KERN_INFO "blocksize in superblock=%lu\n",sb->s_blocksize);	
//	bh = sb_bread(sb, 0);
//	char *ptr;
//	ptr= (char*)bh->b_data;
//	ptr +=512;
	sb_disk = (struct SupBlock *)my_sb_bread(sb,SUP_BLK_STARTS_AT,&bh);
	printk(KERN_INFO "filename=%s\n",sb_disk->sb_vname);
	printk(KERN_INFO "The filesystem  magic number obtained in disk is: [%d]\n",
	       sb_disk->sb_magic);

	if (unlikely(sb_disk->sb_magic != HGFS_MAGIC)) {
		printk(KERN_ERR
		       "The filesystem that you try to mount is not of type hgfs. Magic number mismatch.");
		goto release;
	}

/*	if (unlikely(sb_disk->block_size != BLKSIZE)) {
		printk(KERN_ERR
		       "HGfs seem to be formatted using a non-standard block size.");
		goto release;
	}
*/
	printk(KERN_INFO
	       "hgfs filesystem of version formatted with a block size of [%d] detected in the device.\n",
	        BLKSIZE);

	/* A magic number that uniquely identifies our filesystem type */
	sb->s_magic = HGFS_MAGIC;

	/* For all practical purposes, we will be using this s_fs_info as the super block */
	sb->s_fs_info = sb_disk;

	sb->s_maxbytes = BLKSIZE;
	sb->s_op = &hgfs_sops;

	root_inode = new_inode(sb);
	root_inode->i_ino = DISK_ROOT_INODE_NO;
	inode_init_owner(root_inode, NULL, S_IFDIR);
	root_inode->i_sb = sb;
	root_inode->i_op = &hgfs_inode_ops;
	root_inode->i_fop = &hgfs_dir_operations;
	root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime =
	    CURRENT_TIME;

	root_inode->i_private =
	    hgfs_get_inode(sb, DISK_ROOT_INODE_NO);
	struct INode* ptr= root_inode->i_private;

	printk(KERN_INFO "ROOT inode details: size=%d  mode=%d \n", ptr->i_size,ptr->i_mode);
	if (unlikely (!S_ISDIR(ptr->i_mode)))
		printk(KERN_INFO "ROOT ISnot A DIR\n");
	else
		printk(KERN_INFO "ROOT IS A DIR\n");
 
	/* TODO: move such stuff into separate header. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
	sb->s_root = d_make_root(root_inode);
#else
	sb->s_root = d_alloc_root(root_inode);
	if (!sb->s_root)
		iput(root_inode);
#endif

	if (!sb->s_root) {
		ret = -ENOMEM;
		goto release;
	}

	ret = 0;
release:
	brelse(bh);

	return ret;
}





static struct dentry *hgfs_mount(struct file_system_type *fs_type,
				     int flags, const char *dev_name,
				     void *data)
{
	struct dentry *ret;

	ret = mount_bdev(fs_type, flags, dev_name, data, hgfs_fill_super);

	if (unlikely(IS_ERR(ret)))
		printk(KERN_ERR "Error mounting hgfs");
	else
		printk(KERN_INFO "hgfs is succesfully mounted on [%s]\n",
		       dev_name);

	return ret;
}



static void hgfs_kill_superblock(struct super_block *sb)
{
	printk(KERN_INFO
	       "hgfs superblock is destroyed. Unmount succesful.\n");

	kill_block_super(sb);
	return;
}



struct file_system_type hgfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "hgfs",
	.mount = hgfs_mount,
	.kill_sb = hgfs_kill_superblock,
	.fs_flags = FS_REQUIRES_DEV,
};

static int hgfs_init(void)
{
	int ret;

	hgfs_inode_cachep = kmem_cache_create("hgfs_inode_cache",
	                                     sizeof(struct INode),
	                                     0,
	                                     (SLAB_RECLAIM_ACCOUNT| SLAB_MEM_SPREAD),
	                                     NULL);
	if (!hgfs_inode_cachep) {
		return -ENOMEM;
	}

	ret = register_filesystem(&hgfs_fs_type);
	if (likely(ret == 0))
		printk(KERN_INFO "Sucessfully registered hgfs\n");
	else
		printk(KERN_ERR "Failed to register hgfs. Error:[%d]", ret);

	return ret;
}

static void hgfs_exit(void)
{
	int ret;
	ret = unregister_filesystem(&hgfs_fs_type);
	kmem_cache_destroy(hgfs_inode_cachep);

	if (likely(ret == 0))
		printk(KERN_INFO "Sucessfully unregistered hgfs\n");
	else
		printk(KERN_ERR "Failed to unregister hgfs. Error:[%d]",
		       ret);
}

module_init(hgfs_init);
module_exit(hgfs_exit);

MODULE_LICENSE("CC0");
MODULE_AUTHOR("HRISHIKESH_GOYAL");
/*
struct inode*  hgfs_alloc_inode(struct super_block *sb)
{
    char buf[BLKSIZE * 2];
	struct buffer_head* bh;
    struct SupBlock* sptr= (struct SupBlock*) my_sb_bread(sb,SUP_BLK_STARTS_AT,&bh);

    if (sptr->sb_nfreeino==0) {
        printk(KERN_INFO "Inode alloc  failed,max limit has already reached\n");
        return NULL;
    }
    sptr->sb_nfreeino--;
	/*
    if (sptr->sb_freeinoindex==(BLKSIZE-54)/sizeof(short))//if list becomes empty
    {
        // then scan the inode block to search free inodes and fill again the list in superblk
        int i;
        char buff[BLKSIZE];
        int done=0;
		struct buffer_head* bh2;
        for (i=0; i< 8 && done==0 ; i++)
        {
            
            struct INode* iptr=(struct INode*)my_sb_bread(sb,INODE_BLK_STARTS_AT+i,&bh2) ;
            int j;
            for (j=0; j< BLKSIZE/sizeof(struct INode) && done==0 ; j++,iptr++)
            {
                if (iptr->i_lnk ==0) // no of hardlinks==0
                {
                    int ino_of_this_inode = i* (BLKSIZE/sizeof(struct INode)) + j + 1;

                    sptr->sb_freeinoindex--;
                    sptr->sb_freeinos[sptr->sb_freeinoindex]=ino_of_this_inode;

                    // if list is full now
                    if (sptr->sb_freeinoindex==0)
                        done=1;
                }
            }
        }
    }------------------------------


    int free_inode= sptr->sb_freeinos[sptr->sb_freeinoindex];

    // Freeze the changes in super block
    sptr->sb_freeinoindex++;
	
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	
  //  writeSuper(dev,(struct SupBlock*)buf);

	new_free_inode = new_inode(sb);
	new_free_inode->i_ino = free_inode;
	inode_init_owner(new_free_inode, NULL, S_IFDIR);
	new_free_inode->i_sb = sb;
//	root_inode->i_op = &hgfs_inode_ops;
//	root_inode->i_fop = &hgfs_dir_operations;
	new_free_inode->i_atime = new_free_inode->i_mtime = new_free_inode->i_ctime =
	    CURRENT_TIME;
	
	return  new_free_inode;
	
  //  return free_inode;

}


*/


