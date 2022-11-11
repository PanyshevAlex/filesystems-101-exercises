#include <solution.h>
#include <unistd.h>
#include <ext2fs/ext2fs.h>
#include <string.h>
#include <sys/stat.h>


int read_copy(int img, unsigned number_block, unsigned block_size, char* block_buf, int out, unsigned* curr_size)
{
	ssize_t read_size = pread(img, block_buf, block_size, number_block *  block_size);
	if (read_size < 0)
		return -1;

	if (*curr_size > block_size)
	{
		unsigned write_count = write(out, block_buf, block_size);
		if (write_count != block_size)
			return -errno;
		*curr_size -= block_size;
	}
	else
	{
		unsigned write_count = write(out, block_buf, *curr_size);
		if (write_count != *curr_size)
			return -errno;
		*curr_size = 0;
	}

	return 0;
}

int read_copy_indirect(int img, unsigned number_block, unsigned block_size, char* block_buf, int out, unsigned* curr_size)
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
		
		int res = read_copy(img, indirect_block.idArray[j], block_size, block_buf_ind, out, curr_size);
		if (res < 0)
			return -errno;
	}

	free(block_buf_ind);
	return 0;
}

int read_copy_double_indirect(int img, unsigned number_block, unsigned block_size, char* block_buf, int out, unsigned* curr_size)
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
		
		int res = read_copy_indirect(img, indirect_block.idArray[j], block_size, block_buf_ind, out, curr_size);
		if (res < 0)
			return -errno;
	}

	free(block_buf_ind);
	return 0;
}

int copy_file(int img, unsigned block_size, struct ext2_inode* inode, int out)
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
			res = read_copy(img, inode->i_block[i], block_size, block_buf, out, &curr_size);
			if (res < 0)
				return -errno;
		}
		else if (i == EXT2_IND_BLOCK)
		{
			res = read_copy_indirect(img, inode->i_block[i], block_size, block_buf, out, &curr_size);
			if (res < 0)
				return -errno;
		}
		else if (i == EXT2_DIND_BLOCK)
		{
			res = read_copy_double_indirect(img, inode->i_block[i], block_size, block_buf, out, &curr_size);
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

	return 0;
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
	free(buf);
	return -ENOENT;
}

int get_inode_by_path(int fd, int inode_number, char* path, const struct ext2_super_block* sb)
{
	struct ext2_inode inode = {};
	size_t block_size = 1024 << sb->s_log_block_size;
	struct ext2_group_desc group_desc = {};

	off_t gd_offset = SUPERBLOCK_OFFSET + sizeof(*sb) + (inode_number - 1) / sb->s_inodes_per_group * sizeof(group_desc);
	pread(fd, &group_desc, sizeof(group_desc), gd_offset);

	pread(fd, &inode, sizeof(inode), block_size * group_desc.bg_inode_table + (inode_number - 1) % sb->s_inodes_per_group * sb->s_inode_size);

	if ((inode_number == 2) && ((inode.i_mode & LINUX_S_IFDIR) == 0))
		return -ENOTDIR;
	char* next_path = strchr(path, '/');
	if (next_path != NULL)
	{
		if ((inode.i_mode & LINUX_S_IFDIR) == 0)
			return -ENOTDIR;
		*(next_path++) = '\0';
		int next_nr = get_inode(fd, path, block_size, &inode);
		return get_inode_by_path(fd, next_nr, next_path, sb);
	}
	else
		return get_inode(fd, path, block_size, &inode);
}

int dump_file(int img, const char *path, int out)
{
	(void)path;
	struct ext2_super_block sb = {};
	struct ext2_group_desc group_desc = {};
	struct ext2_inode inode = {};
	
	ssize_t read_size = pread(img, &sb, sizeof(sb), 1024);
	if (read_size < 0)
		return -errno;
	char* path_tmp = strdup(path);
	int inode_number = get_inode_by_path(img, 2, path_tmp+1, &sb);
	free(path_tmp);
	// int inode_number = 2;
	if (inode_number < 0)
		return inode_number;

	unsigned block_size = 1024 << sb.s_log_block_size;

	unsigned bg_number = (inode_number - 1) / sb.s_inodes_per_group;
	unsigned index = (inode_number - 1) % sb.s_inodes_per_group;



	read_size = pread(img, &group_desc, sizeof(group_desc), ((block_size > 1024) ? 1 : 2) * block_size + bg_number * sizeof(struct ext2_group_desc));
	if (read_size < 0)
		return -errno;



	read_size = pread(img, &inode, sizeof(inode), group_desc.bg_inode_table * block_size + index * sb.s_inode_size);
	if (read_size < 0)
		return -errno;

	int res = copy_file(img, block_size, &inode, out);
	if (res < 0)
			return -errno;
	return 0;
}
