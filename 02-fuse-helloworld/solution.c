#include <solution.h>

#include <fuse.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>
#include <stdlib.h>	
#include <stddef.h>
#include <stdio.h>
const char *NAME = "hello";

static int read_hello(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *info)
{
	(void)info;
	if (strcmp(path+1, NAME) != 0)
		return -ENOENT;
	struct fuse_context *context = fuse_get_context();
	char *out = (char *)malloc(sizeof(char)*64);
	sprintf(out, "hello, %d\n", context->pid);
	int len = (int)strlen(out);
	if (offset < len)
	{
		if ((offset + (int)size) > len)
		{
			memcpy(buf, out, len - offset);
			return len - offset;
		}
		memcpy(buf, out, size);
		return size;
	}
	return 0;
}

static int readdir_hello(const char *path, void *buf, fuse_fill_dir_t fill, off_t offset, struct fuse_file_info *fi, enum fuse_readdir_flags flags)
{
	(void)offset;
	(void)fi;
	(void)flags;
	if (strcmp(path, "/") != 0)
		return -ENOENT;
	fill(buf, ".", NULL, 0, 0);
	fill(buf, "..", NULL, 0, 0);
	fill(buf, NAME, NULL, 0, 0);
	return 0;
}

static int getattr_hello(const char *path, struct stat *buf, struct fuse_file_info *fi)
{
	(void)fi;
	memset(buf, 0, sizeof(*buf));

	if (strcmp(path, "/") == 0)
	{
		struct fuse_context *context = fuse_get_context();
		buf->st_mode = S_IFDIR | 0775;
		buf->st_nlink = 2;
		buf->st_uid = context->uid;
		buf->st_gid = context->gid;
		return 0;
	}
	else if (strcmp(path+1, NAME) == 0)
	{
		struct fuse_context *context = fuse_get_context();
		buf->st_mode = S_IFREG | 0400;
		buf->st_nlink = 1;
		buf->st_size = 64;
		buf->st_uid = context->uid;
		buf->st_gid = context->gid;
		return 0;
	}
	return -ENOENT;
}

static int open_hello(const char *path, struct fuse_file_info *info)
{
	if ((info->flags & O_ACCMODE) != O_RDONLY)
		return -EROFS;
	if (strcmp(path+1, NAME) != 0)
		return -ENOENT;
	return 0;
}

static int create_hello(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)path;
	(void)mode;
	(void)fi;
	return -EROFS;
}

static int write_hello(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	(void)buf;
	(void)size;
	(void)offset;
	(void)fi;
	if (strcmp(path+1, NAME) == 0)
		return -EROFS;
	return -ENOENT;
}

static void* init_hello(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
	(void)conn;
	cfg->uid = getuid();
	cfg->gid = getgid();
	cfg->kernel_cache = 1;
	return NULL;
}

int opendir_hello(const char* path, struct fuse_file_info* info)
{
	(void)info;
	if (strcmp(path, "/") == 0)
		return 0;
	return -ENOENT;
}

static const struct fuse_operations hellofs_ops = {
	/* implement me */
	.read = read_hello,
	.readdir = readdir_hello,
	.getattr = getattr_hello,
	.open = open_hello,
	.create = create_hello,
	.write = write_hello,
	.init = init_hello,
	.opendir = opendir_hello,
};

int helloworld(const char *mntp)
{
	char *argv[] = {"exercise", "-f", (char *)mntp, NULL};
	return fuse_main(3, argv, &hellofs_ops, NULL);
}
