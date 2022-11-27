#include <solution.h>
#include <unistd.h>
#include <ext2fs/ext2fs.h>
#include <string.h>
#include <sys/stat.h>
#include <fuse.h>

int g_img = 0;
unsigned g_block_size = 0;
struct ext2_super_block g_sb = {};

// readfile

struct readbuf
{
	char *buf;
	int offset;
};

int read_copy(int img, unsigned number_block, unsigned block_size, char* block_buf, struct readbuf *read_buf, unsigned* curr_size)
{
	ssize_t read_size = pread(img, block_buf, block_size, number_block *  block_size);
	if (read_size < 0)
		return -1;

	if (*curr_size > block_size)
	{
		// unsigned write_count = write(out, block_buf, block_size);
		memcpy(read_buf->buf+read_buf->offset, block_buf, block_size);
		read_buf->offset += block_size;
		*curr_size -= block_size;
	}
	else
	{
		// unsigned write_count = write(out, block_buf, *curr_size);
		memcpy(read_buf->buf+read_buf->offset, block_buf, *curr_size);
		read_buf->offset += *curr_size;
		*curr_size = 0;
	}

	return 0;
}

int read_copy_indirect(int img, unsigned number_block, unsigned block_size, char* block_buf, struct readbuf *read_buf, unsigned* curr_size)
{
	ssize_t read_size = pread(img, block_buf, block_size, number_block *  block_size);
	if (read_size < 0)
		return -1;

	union Indirect_block
	{
		char* rawBuffer;
		int32_t* idArray; 
	} indirect_block;

	indirect_block.rawBuffer = block_buf;
	char* block_buf_ind = (char*) malloc(block_size);

	for (unsigned j = 0; j < block_size / 4 && *curr_size > 0; ++j)
	{
		if (indirect_block.idArray[j] == 0)
			break;
		
		int res = read_copy(img, indirect_block.idArray[j], block_size, block_buf_ind, read_buf, curr_size);
		if (res < 0)
			return -errno;
	}

	free(block_buf_ind);
	return 0;
}

int read_copy_double_indirect(int img, unsigned number_block, unsigned block_size, char* block_buf, struct readbuf *read_buf, unsigned* curr_size)
{
	ssize_t read_size = pread(img, block_buf, block_size, number_block *  block_size);
	if (read_size < 0)
		return -1;

	union Indirect_block
	{
		char* rawBuffer;
		int32_t* idArray; 
	} indirect_block;

	indirect_block.rawBuffer = block_buf;
	char* block_buf_ind = (char*) malloc(block_size);

	for (unsigned j = 0; j < block_size / 4 && *curr_size > 0; ++j)
	{
		if (indirect_block.idArray[j] == 0)
			break; 
		
		int res = read_copy_indirect(img, indirect_block.idArray[j], block_size, block_buf_ind, read_buf, curr_size);
		if (res < 0)
			return -errno;
	}

	free(block_buf_ind);
	return 0;
}

int copy_file(int img, unsigned block_size, struct ext2_inode* inode, struct readbuf *read_buf, size_t size)
{
	char* block_buf = (char*) malloc(block_size);
	unsigned curr_size = inode->i_size;
	
	for (unsigned i = 0; i < EXT2_N_BLOCKS && curr_size > 0; ++i)
	{
		if (inode->i_block[i] == 0)
			break; 
		int res;
		if (i < EXT2_NDIR_BLOCKS)
		{
			res = read_copy(img, inode->i_block[i], block_size, block_buf, read_buf, &curr_size);
			if (res < 0)
				return -errno;
		}
		else if (i == EXT2_IND_BLOCK)
		{
			res = read_copy_indirect(img, inode->i_block[i], block_size, block_buf, read_buf, &curr_size);
			if (res < 0)
				return -errno;
		}
		else if (i == EXT2_DIND_BLOCK)
		{
			res = read_copy_double_indirect(img, inode->i_block[i], block_size, block_buf, read_buf, &curr_size);
			if (res < 0)
				return -errno;
		}
		else
		{
			free(block_buf);

			return -1;
		}
	}

	free(block_buf);

	return (int)size;
}

int get_d_block(const char* buf, const char* name, size_t block_size)
{
	size_t offset = 0;
	while (offset - EXT2_NAME_LEN + sizeof(struct ext2_dir_entry) < block_size) 
	{
		const struct ext2_dir_entry* entry = (const struct ext2_dir_entry*)(buf + offset);
		if (entry->rec_len == 0) 
			return 0;
		else if (entry->inode == 0) 
		{ 
			offset += entry->rec_len;
			continue;
		} 
		unsigned name_len = (unsigned)ext2fs_dirent_name_len(entry);
		if (strncmp(name, entry->name, name_len) == 0)
			return entry->inode;

		offset += entry->rec_len;
	}
	return 0;
}

int get_id_block(int fd, const unsigned* indir_block, const char* name, size_t block_size)
{
	ssize_t read_size;
	char *buf = (char*)malloc(block_size);
	int inode_number;
	for (unsigned i = 0; i < block_size / sizeof(unsigned); i++) 
	{
		if (indir_block[i] == 0) 
		{
			free(buf);
			return 0;
		}
		read_size = pread(fd, buf, block_size, block_size * indir_block[i]);
		if (read_size < 0)
			return -errno;
		if ((inode_number = get_d_block(buf, name, block_size)) != 0) 
		{
			free(buf);
			return inode_number;
		}
	}
	free(buf);
	return 0;
}

int get_did_block(int fd, const unsigned* dindir_block, const char* name, size_t block_size)
{
	ssize_t read_size;
	unsigned *buf = (unsigned*)malloc(block_size);
	int inode_number;
	for (unsigned i = 0; i < block_size / sizeof(unsigned); i++) 
	{
		if (dindir_block[i] == 0) {
			free(buf);
			return 0;
		}
		read_size = pread(fd, (char*)buf, block_size, block_size * dindir_block[i]);
		if (read_size < 0)
			return -errno;
		if ((inode_number = get_id_block(fd, buf, name, block_size)) != 0) {
			free(buf);
			return inode_number;
		}
	}
	free(buf);
	return 0;
}

int get_inode(int fd, const char* name, size_t block_size, const struct ext2_inode* inode)
{
	ssize_t read_size;
	char* buf = malloc(block_size);
	int inode_number = 0;
	for (unsigned i = 0; i < EXT2_NDIR_BLOCKS; i++) 
	{
		read_size = pread(fd, buf, block_size, block_size * inode->i_block[i]);
		if (read_size < 0)
		{
			free(buf);
			return -errno;
		}
		inode_number = get_d_block(buf, name, block_size);

		if (inode_number != 0) 
		{
			free(buf);
			return inode_number;
		}
	}
	if (inode->i_block[EXT2_IND_BLOCK] != 0) 
	{
		read_size = pread(fd, buf, block_size, block_size * inode->i_block[EXT2_IND_BLOCK]);
		if (read_size < 0)
		{
			free(buf);
			return -errno;
		}
		if ((inode_number = get_id_block(fd, (unsigned*)buf, name, block_size)) != 0) 
		{
			free(buf);
			return inode_number;
		}
	}
	if (inode->i_block[EXT2_DIND_BLOCK] != 0) 
	{
		read_size = pread(fd, buf, block_size, block_size * inode->i_block[EXT2_DIND_BLOCK]);
		if (read_size < 0)
		{
			free(buf);
			return -errno;
		}
		if ((inode_number = get_did_block(fd, (unsigned*)buf, name, block_size)) != 0) 
		{
			free(buf);
			return inode_number;
		}
	}
	free(buf);
	return -ENOENT;
}

int get_inode_by_path(int fd, int inode_number, char* path, const struct ext2_super_block* sb)
{
	struct ext2_inode inode = {};
	size_t block_size = 1024 << sb->s_log_block_size;
	struct ext2_group_desc group_desc = {};

	off_t gd_offset = SUPERBLOCK_OFFSET + sizeof(*sb) + (inode_number - 1) / sb->s_inodes_per_group * sizeof(group_desc);
	ssize_t read_size = pread(fd, &group_desc, sizeof(group_desc), gd_offset);
	if (read_size < 0)
		return -errno;

	read_size = pread(fd, &inode, sizeof(inode), block_size * group_desc.bg_inode_table + (inode_number - 1) % sb->s_inodes_per_group * sb->s_inode_size);
	if (read_size < 0)
		return -errno;
	char* next_path = strchr(path, '/');
	if (next_path != NULL)
	{
		*(next_path++) = '\0';
		int next_nr = get_inode(fd, path, block_size, &inode);
		return get_inode_by_path(fd, next_nr, next_path, sb);
	}
	else
		return get_inode(fd, path, block_size, &inode);
}

// readdir

mode_t ext2_ft_nix(int ext2_mode)
{
	switch (ext2_mode) 
	{
	case EXT2_FT_REG_FILE:
		return S_IFREG;
	case EXT2_FT_DIR:
		return S_IFDIR;
	case EXT2_FT_CHRDEV:
		return S_IFCHR;
	case EXT2_FT_BLKDEV:
		return S_IFBLK;
	case EXT2_FT_FIFO:
		return S_IFIFO;
	case EXT2_FT_SOCK:
		return S_IFSOCK;
	case EXT2_FT_SYMLINK:
		return S_IFLNK;
	default:
		return 0;
	}
}


int read_block(unsigned block_size, const char *block_buf, void *fuse_buf, fuse_fill_dir_t fill_callback)
{
	struct ext2_dir_entry_2 *entry = (struct ext2_dir_entry_2 *) block_buf;
	char filename[EXT2_NAME_LEN + 1] = {};

	unsigned left_size = block_size;
	
	while (left_size && entry->inode != 0)
	{
		memcpy(filename, entry->name, entry->name_len);
		filename[entry->name_len] = '\0';

		// report_file(entry->inode, type, filename);
		struct stat stat = {};
		stat.st_ino = entry->inode;
		stat.st_mode = ext2_ft_nix(entry->file_type);
		fill_callback(fuse_buf, filename, &stat, 0, 0);
		block_buf += entry->rec_len;
		left_size -= entry->rec_len;

		entry = (struct ext2_dir_entry_2 *)block_buf;
	}

	return 0;
}


int read_block_indirect(int img, unsigned block_size, const char *block_buf, void *fuse_buf, fuse_fill_dir_t fill_callback)
{
	union Indirect_block
	{
		const char *rawBuffer;
		int32_t *idArray;
	}indirect_block;
	indirect_block.rawBuffer = block_buf;
	int res;
	char *block_buf_ind = (char *)malloc(block_size);

	for (unsigned j = 0; j < block_size / 4; ++j)
	{
		if (indirect_block.idArray[j] == 0)
		{
			free(block_buf_ind);
			return 0;
		}

		ssize_t read_size = pread(img, block_buf_ind, block_size, indirect_block.idArray[j] * block_size);
		if (read_size < 0)
			return -errno;
		res = read_block(block_size, block_buf_ind, fuse_buf, fill_callback);
		if (res < 0)
			return -errno;
	}

	free(block_buf_ind);
	return 0;
}

int read_block_double_indirect(int img, unsigned block_size, const char *block_buf, void *fuse_buf, fuse_fill_dir_t fill_callback)
{
	union Indirect_block
	{
		const char *rawBuffer;
		int32_t *idArray;
	}indirect_block;
	indirect_block.rawBuffer = block_buf;
	int res;
	char *block_buf_ind = (char *)malloc(block_size);

	for (unsigned j = 0; j < block_size / 4; ++j)
	{
		if (indirect_block.idArray[j] == 0)
		{
			free(block_buf_ind);
			return 0;
		}

		ssize_t read_size = pread(img, block_buf_ind, block_size, indirect_block.idArray[j] * block_size);
		if (read_size < 0)
			return -errno;
		res = read_block_indirect(img, block_size, block_buf_ind, fuse_buf, fill_callback);
		if (res < 0)
			return -errno;
	}

	free(block_buf_ind);
	return 0;
}


int read_dir(int img, unsigned block_size, const struct ext2_inode *inode, void *fuse_buf, fuse_fill_dir_t fill_callback)
{
	int res;
	char *block_buf = (char *)malloc(block_size);

	for (int i = 0; i < EXT2_N_BLOCKS; ++i)
	{
		if (inode->i_block[i] == 0)
		{
			free(block_buf);
			return 0;
		}
		ssize_t read_size = pread(img, block_buf, block_size, inode->i_block[i] * block_size);
		if (read_size < 0)
			return -errno;
		
		if (i < EXT2_NDIR_BLOCKS)
		{
			res = read_block(block_size, block_buf, fuse_buf, fill_callback);
			if (res < 0)
				return -errno;
		}
		else if (i == EXT2_IND_BLOCK)
		{
			res = read_block_indirect(img, block_size, block_buf, fuse_buf, fill_callback);
			if (res < 0)
				return -errno;
		}
		else if (i == EXT2_DIND_BLOCK)
		{
			res = read_block_double_indirect(img, block_size, block_buf, fuse_buf, fill_callback);
			if (res < 0)
				return -errno;
		}
	}

	free(block_buf);
	return 0;
}

int get_inode_number(int img, const char *path, const struct ext2_super_block *sb)
{
	if (strcmp(path, "/") == 0)
		return 2;
	char *path_copy = strdup(path);
	int inode_number = get_inode_by_path(img, 2, path_copy+1, sb);
	free(path_copy);
	return inode_number;
}

// fuse operations

static int write_ext2(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	(void)path;
	(void)buf;
	(void)size;
	(void)offset;
	(void)fi;
	return -EROFS;
}

static int read_ext2(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	(void)path;
	(void)buf;
	(void)size;
	(void)offset;
	(void)fi;
	struct ext2_group_desc group_desc = {};
	struct ext2_inode inode = {};
	char* path_tmp = strdup(path);
	int inode_number = get_inode_number(g_img, path_tmp, &g_sb);
	if (inode_number < 0)
	{
		free(path_tmp);
		return inode_number;
	}
	unsigned bg_number = (inode_number - 1) / g_sb.s_inodes_per_group;
	unsigned index = (inode_number - 1) % g_sb.s_inodes_per_group;

	int read_size = pread(g_img, &group_desc, sizeof(group_desc), ((g_block_size > 1024) ? 1 : 2) * g_block_size + bg_number * sizeof(struct ext2_group_desc));
	if (read_size < 0)
	{
		free(path_tmp);
		return -errno;
	}
	read_size = pread(g_img, &inode, sizeof(inode), group_desc.bg_inode_table * g_block_size + index * g_sb.s_inode_size);
	if (read_size < 0)
	{
		free(path_tmp);
		return -errno;
	}
	struct readbuf read_buf;
	read_buf.offset = 0;
	read_buf.buf = buf;
	free(path_tmp);
	return copy_file(g_img, g_block_size, &inode, &read_buf, size);
}

static int readdir_ext2(const char *path, void *buf, fuse_fill_dir_t fill, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{


	(void)offset;
	(void)fi;
	(void)flags;
	struct ext2_group_desc group_desc = {};
	struct ext2_inode inode = {};
	char* path_tmp = strdup(path);
	int inode_number = get_inode_number(g_img, path_tmp, &g_sb);
	if (inode_number < 0)
	{
		free(path_tmp);
		return inode_number;
	}
	unsigned bg_number = (inode_number - 1) / g_sb.s_inodes_per_group;
	unsigned index = (inode_number - 1) % g_sb.s_inodes_per_group;

	int read_size = pread(g_img, &group_desc, sizeof(group_desc), ((g_block_size > 1024) ? 1 : 2) * g_block_size + bg_number * sizeof(struct ext2_group_desc));
	if (read_size < 0)
	{
		free(path_tmp);
		return -errno;
	}



	read_size = pread(g_img, &inode, sizeof(inode), group_desc.bg_inode_table * g_block_size + index * g_sb.s_inode_size);
	if (read_size < 0)
	{
		free(path_tmp);
		return -errno;
	}
	free(path_tmp);
	return read_dir(g_img, g_block_size, &inode, buf, fill);
}

static int getattr_ext2(const char *path, struct stat *buf, struct fuse_file_info *fi)
{
	(void)path;
	(void)buf;
	(void)fi;
	struct ext2_group_desc group_desc = {};
	struct ext2_inode inode = {};
	char* path_tmp = strdup(path);
	int inode_number = get_inode_number(g_img, path_tmp, &g_sb);
	if (inode_number < 0)
	{
		free(path_tmp);
		return inode_number;
	}
	
	unsigned bg_number = (inode_number - 1) / g_sb.s_inodes_per_group;
	unsigned index = (inode_number - 1) % g_sb.s_inodes_per_group;

	int read_size = pread(g_img, &group_desc, sizeof(group_desc), ((g_block_size > 1024) ? 1 : 2) * g_block_size + bg_number * sizeof(struct ext2_group_desc));
	if (read_size < 0)
	{
		free(path_tmp);
		return -errno;
	}



	read_size = pread(g_img, &inode, sizeof(inode), group_desc.bg_inode_table * g_block_size + index * g_sb.s_inode_size);
	if (read_size < 0)
	{
		free(path_tmp);
		return -errno;
	}
	buf->st_ino = inode_number;
	buf->st_mode = inode.i_mode;
	buf->st_nlink = inode.i_links_count;
	buf->st_uid = inode.i_uid;
	buf->st_gid = inode.i_gid;
	
	buf->st_size = inode.i_size;
	buf->st_blksize = g_block_size;
	buf->st_blocks = inode.i_blocks;
	buf->st_atime = inode.i_atime;
	buf->st_mtime = inode.i_mtime;
	buf->st_ctime = inode.i_ctime;
	free(path_tmp);
	return 0;
}

static int open_ext2(const char *path, struct fuse_file_info *fi)
{
	(void)path;
	(void)fi;
	return 0;
}

static int create_ext2(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)path;
	(void)mode;
	(void)fi;
	return -EROFS;
}

static void* init_ext2(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	(void)conn;
	(void)cfg;
	return NULL;
}

int opendir_ext2(const char* path, struct fuse_file_info* info)
{
	(void)path;
	(void)info;
	return 0;
}

static const struct fuse_operations ext2_ops = {
	/* implement me */
	.read = read_ext2,
	.readdir = readdir_ext2,
	.getattr = getattr_ext2,
	.open = open_ext2,
	.create = create_ext2,
	.write = write_ext2,
	.init = init_ext2,
	.opendir = opendir_ext2,
};

int ext2fuse(int img, const char *mntp)
{
	g_img = img;
	ssize_t read_size = pread(img, &g_sb, sizeof(g_sb), SUPERBLOCK_OFFSET);
	if (read_size < 0)
		return -errno;
	g_block_size = 1024 << g_sb.s_log_block_size;

	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &ext2_ops, NULL);
}
