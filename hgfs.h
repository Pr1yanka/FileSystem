#define MAXFNAME	10
#define BLKSIZE		512

#define INODE_BLK_STARTS_AT 3
#define DATA_BLK_STARTS_AT 11
#define SUP_BLK_STARTS_AT 1

#define HGFS_MAGIC 100
#define SUP_BLK_STARTS_AT 1
#define DISK_ROOT_INODE_NO 1


struct SupBlock {
	char sb_vname[MAXFNAME];
	int sb_magic;
	int	sb_nino;
	int	sb_nblk;
	int	sb_nrootdir;
	int	sb_nfreeblk;
	int	sb_nfreeino;
	int	sb_flags;
	unsigned short sb_freeblks[BLKSIZE/sizeof(unsigned short)];
	int	sb_freeblkindex;
	int	sb_freeinoindex;
	unsigned int	sb_chktime;
	unsigned int	sb_ctime;
	unsigned short sb_freeinos[(BLKSIZE - (54))/2];
};

struct INode {
	unsigned int	i_size;
	unsigned int	i_atime;
	unsigned int	i_ctime;
	unsigned int	i_mtime;
	unsigned short	i_blks[13];
	short		i_mode;
	unsigned char	i_uid;
	unsigned char	i_gid;
	unsigned char	i_gen;
	unsigned char	i_lnk;
};


#define ROOTDIRSIZE	((4 * 512)/sizeof(struct OnDiskDirEntry))

#define INODETABSIZE	(( 512 / sizeof(struct INode))*8)


struct OnDiskDirEntry {
	char	d_name[MAXFNAME];
	unsigned short	d_ino;
};

#define  HGFS_SB(sb)  sb->s_fs_info
#define  HGFS_INODE(inode) inode->i_private 


