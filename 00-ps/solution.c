#include <solution.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <inttypes.h>
#define PROC_PATH "/proc"

typedef struct {
	char* buf;
	ssize_t buf_size;
} d_buf;

typedef struct {
	char** strings;
	ssize_t size;
} s_list;

int is_pid(const char* filename)
{
	unsigned cursor = 0;
	while ((filename[cursor] >= '0') && (filename[cursor] <= '9'))
		cursor++;
	return filename[cursor] == '\0' ? 1 : 0;
}

ssize_t check_exe(int dirfd, const char* path, d_buf* buf)
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

ssize_t readfile(int dirfd, const char* filename, d_buf* buf)
{
	int filefd = openat(dirfd, filename, O_RDONLY);
	if (filefd < 0)
		return -1;
	ssize_t read_size = 0, total_read = 0;
	while ((read_size = read(filefd, buf->buf + total_read, buf->buf_size - total_read)) == (buf->buf_size - total_read)) {
		total_read += read_size;
		ssize_t new_buf_size = buf->buf_size * 2;
		char* new_buf = (char*) realloc(buf->buf, new_buf_size);
		if (new_buf == NULL)
			return -1;
		buf->buf = new_buf;
		buf->buf_size = new_buf_size;
	}
	if (read_size < 0)
		return -1;
	total_read += read_size;
	return total_read;
}

ssize_t make_array(char* buf, ssize_t buf_size, s_list* list)
{
	ssize_t str_count = 0;
	char* cursor = buf;
	for (ssize_t i = 0; i < buf_size; i++) {
		if (buf[i] == '\n')
			buf[i] = '\0';
		if (buf[i] == '\0') {
			str_count++;
			while (str_count + 1 >= list->size) {
				ssize_t new_size = list->size * 2;
				char** new_strings = realloc(list->strings, new_size * (sizeof(char*)));
				if (new_strings == NULL)
					return -1;
				list->strings = new_strings;
				list->size = new_size;
			}
			list->strings[str_count - 1] = cursor;
			cursor = buf + i + 1;
		}
	}
	list->strings[str_count] = NULL;
	return str_count;
}

void ps(void)
{
	int proc_dirfd = open(PROC_PATH, O_DIRECTORY);
	if (proc_dirfd < 0) {
		report_error(PROC_PATH, errno);
		return;
	}
	DIR* proc_dir_struct = fdopendir(proc_dirfd);
	if (proc_dir_struct == NULL) {
		report_error(PROC_PATH, errno);
		return;
	}
	struct dirent* cur_entry = NULL;
	d_buf exe_buf = {(char*) malloc(256), 256};
	d_buf argv_buf = {(char*) malloc(256), 256};
	d_buf envp_buf = {(char*) malloc(1024), 1024};
	s_list arg_list = {(char**) malloc (32 * sizeof(char*)), 32};
	s_list env_list = {(char**) malloc(64 * sizeof(char*)), 64};
	while ((cur_entry = readdir(proc_dir_struct)) != NULL) {
		if (!is_pid(cur_entry->d_name))
            continue;
		int dirfd = openat(proc_dirfd, cur_entry->d_name, O_DIRECTORY);
		if (dirfd < 0) {
            char path_err[256];
            snprintf(path_err, 256, "%s/%s", PROC_PATH, cur_entry->d_name);
			report_error(path_err, errno);
			continue;
		}
		pid_t cur_pid = strtol(cur_entry->d_name, NULL, 10);
		ssize_t exe_size = check_exe(dirfd, "exe", &exe_buf);
		if (exe_size < 0) {
            char path_err[256];
            snprintf(path_err, 256, "%s/%s/%s", PROC_PATH, cur_entry->d_name, "exe");
			report_error(path_err, errno);
			continue;
		}
		ssize_t argv_size = readfile(dirfd, "cmdline", &argv_buf);
		if (argv_size < 0) {
            char path_err[256];
            snprintf(path_err, 256, "%s/%s/%s", PROC_PATH, cur_entry->d_name, "cmdline");
			report_error(path_err, errno);
			continue;
		}
		ssize_t envp_size = readfile(dirfd, "environ", &envp_buf);
		if (envp_size < 0) {
            char path_err[256];
            snprintf(path_err, 256, "%s/%s/%s", PROC_PATH, cur_entry->d_name, "environ");
			report_error(path_err, errno);
			continue;
		}
		make_array(argv_buf.buf, argv_size, &arg_list);
		make_array(envp_buf.buf, envp_size, &env_list);
		report_process(cur_pid, exe_buf.buf, arg_list.strings, env_list.strings);
	}
	free(exe_buf.buf);
	free(argv_buf.buf);
	free(envp_buf.buf);
	free(arg_list.strings);
	free(env_list.strings);
	closedir(proc_dir_struct);
}
