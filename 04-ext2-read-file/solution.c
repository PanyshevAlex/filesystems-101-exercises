#include <solution.h>

#include <sys/types.h>
#include <unistd.h>
#include <ext2fs/ext2fs.h>


int read_copy(int img, unsigned number_block, unsigned block_size, char* block_buf, int out, unsigned* curr_size)
{
	ssize_t read_size = pread(img, block_buf, block_size, number_block *  block_size);
	if (read_size < 0)
		return -1;

	if (*curr_size > block_size)
	{
		int write_count = write(out, block_buf, block_size);
		if (write_count != block_size)
			return -errno;
		*curr_size -= block_size;
	}
	else
	{
		int write_count = write(out, block_buf, *curr_size);
		if (write_count != curr_size)
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

int dump_file(int img, int inode_nr, int out)
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

	int res = copy_file(img, block_size, &inode, out);
	if (res < 0)
			return -errno;
	return 0;
}