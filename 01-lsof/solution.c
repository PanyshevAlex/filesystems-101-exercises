#include <solution.h>
#include <sys/stat.h> 
#include <fcntl.h> 
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
int is_pid(const char *name)
{
    int current = 0;
    while (name[current] >= '0' && name[current] <= '9')
        current++;
    return name[current] == '\0' ? 1 : 0;
}

typedef struct {
    char *buf;
    ssize_t buf_size;
} d_buf;

ssize_t areadlinkat(int dirfd, const char* path, d_buf* buf)
{
	ssize_t read_size = readlinkat(dirfd, path, buf->buf, buf->buf_size);
	while (read_size >= buf->buf_size) {
		free(buf->buf);
		buf->buf_size *= 2;
		buf->buf = (char*) malloc(buf->buf_size);
		read_size = readlinkat(dirfd, path, buf->buf, buf->buf_size);
	}
	if (read_size > 0)
		buf->buf[read_size] = '\0';
	return read_size;
}

void lsof(void)
{
    int proc_fd = open("/proc", O_DIRECTORY);
    if (proc_fd < 0)
    {
        report_error("/proc", errno);
        return;
    }
    DIR *proc_dir = fdopendir(proc_fd);
    if (NULL == proc_dir)
    {
        report_error("/proc", errno);
        return;
    }
    d_buf buf = {calloc(256, sizeof(char)), 256};
    struct dirent *entry;
    while ((entry = readdir(proc_dir)) != NULL)
    {
        if (!is_pid(entry->d_name))
            continue;
        char path_buf[512];
        snprintf(path_buf, 512, "/proc/%s/fd", entry->d_name);
        int fd_fd = open(path_buf, O_DIRECTORY);
        if (fd_fd < 0)
        {
            report_error(path_buf, errno);
            continue;
        }
        DIR *fd_entry = fdopendir(fd_fd);
        if (NULL == fd_entry)
        {
            report_error(path_buf, errno);
            close(fd_fd);
            continue;
        }
        // smth
        struct dirent *cur_entry = NULL;
        ssize_t link_size = 0;
        while ((cur_entry = readdir(fd_entry)) != NULL)
        {
            link_size = areadlinkat(fd_fd, cur_entry->d_name, &buf);
            if (link_size < 0)
            {
                char path[512];
                snprintf(path, 512, "%s/%s", path_buf, cur_entry->d_name);
                report_error(path, errno);
                continue;
            }
            if (buf.buf[0] == '/')
                report_file(buf.buf);
        }
        //
        closedir(fd_entry);
        close(fd_fd);
    }
    free(buf.buf);
    closedir(proc_dir);
    close(proc_fd);
}
