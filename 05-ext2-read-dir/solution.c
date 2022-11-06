#include <solution.h>

#include <sys/types.h>
#include <unistd.h>
#include <ext2fs/ext2fs.h>



int read_block(unsigned block_size, const char *block_buf)
{
	struct ext2_dir_entry_2 *entry = (struct ext2_dir_entry_2 *) block_buf;
	char filename[EXT2_NAME_LEN + 1] = {};
	char type;
	unsigned left_size = block_size;
	
	while (left_size && entry->inode != 0)
	{
		memcpy(filename, entry->name, entry->name_len);
		filename[entry->name_len] = '\0';
		if (entry->file_type == EXT2_FT_REG_FILE)
			type = 'f';
		else if (entry->file_type == EXT2_FT_DIR)
			type = 'd';
		else
			type = 'x';
		report_file(entry->inode, type, filename);

		block_buf += entry->rec_len;
		left_size -= entry->rec_len;

		entry = (struct ext2_dir_entry_2 *)block_buf;
	}

	return 0;
}


int read_block_indirect(int img, unsigned block_size, const char *block_buf)
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
		res = read_block(block_size, block_buf_ind);
		if (res < 0)
			return -errno;
	}

	free(block_buf_ind);
	return 0;
}

int read_block_double_indirect(int img, unsigned block_size, const char *block_buf)
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
		res = read_block_indirect(img, block_size, block_buf_ind);
		if (res < 0)
			return -errno;
	}

	free(block_buf_ind);
	return 0;
}


int read_dir(int img, unsigned block_size, const struct ext2_inode *inode)
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
			res = read_block(block_size, block_buf);
			if (res < 0)
				return -errno;
		}
		else if (i == EXT2_IND_BLOCK)
		{
			res = read_block_indirect(img, block_size, block_buf);
			if (res < 0)
				return -errno;
		}
		else if (i == EXT2_DIND_BLOCK)
		{
			res = read_block_double_indirect(img, block_size, block_buf);
			if (res < 0)
				return -errno;
		}
	}

	free(block_buf);
	return 0;
}

int dump_dir(int img, int inode_nr)
{
	struct ext2_super_block sb = {};
	struct ext2_group_desc group_desc = {};
	struct ext2_inode inode = {};
	
	ssize_t read_size = pread(img, &sb, sizeof(sb), 1024);
	if (read_size < 0)
		return -errno;

	unsigned block_size = 1024 << sb.s_log_block_size;

	unsigned bg_number = (inode_nr - 1) / sb.s_inodes_per_group;
	unsigned index = (inode_nr - 1) % sb.s_inodes_per_group;

	read_size = pread(img, &group_desc, sizeof(group_desc), ((block_size > 1024) ? 1 : 2) * block_size + bg_number * sizeof(struct ext2_group_desc));
	if (read_size < 0)
		return -errno;



	read_size = pread(img, &inode, sizeof(inode), group_desc.bg_inode_table * block_size + index * sb.s_inode_size);
	if (read_size < 0)
		return -errno;

	int res = read_dir(img, block_size, &inode);
	if (res < 0)
			return -errno;
	return 0;
}