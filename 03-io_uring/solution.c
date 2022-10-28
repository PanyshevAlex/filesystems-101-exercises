#include <solution.h>
#include <errno.h>
#include <sys/stat.h>
#include <liburing.h>
#include <stdlib.h>

#define ENTRIES 8
#define BLOCKSIZE (256 * 1024)
#define QUEUE_READ_SIZE 4

struct io_data {
    int read;
    off_t first_offset, offset;
    size_t first_len;
    struct iovec iov;
};



static int file_size(int fd, off_t *size)
{
    struct stat st;
    if (fstat(fd, &st) < 0)
        return -1;
    *size = st.st_size;
    return 0;
}

static void entry_write(int out, struct io_uring *ring, struct io_data *data)
{
    struct io_uring_sqe *sqe;
    data->read = 0;
    data->offset = data->first_offset;
    data->iov.iov_base = data + 1;
    data->iov.iov_len = data->first_len;
    sqe = io_uring_get_sqe(ring);
    io_uring_prep_writev(sqe, out, &data->iov, 1, data->offset);
    io_uring_sqe_set_data(sqe, data);
    io_uring_submit(ring);
}

static int entry_read(int in, struct io_uring *ring, off_t size, off_t offset)
{
    struct io_uring_sqe *sqe;
    struct io_data *data;
    data = malloc(size + sizeof(*data));
    sqe = io_uring_get_sqe(ring);
    if (!sqe)
    {
        free(data);
        return 1;
    }
    data->read = 1;
    data->offset = data->first_offset = offset;
    data->iov.iov_base = data + 1;
    data->iov.iov_len = size;
    data->first_len = size;
    io_uring_prep_readv(sqe, in, &data->iov, 1, offset);
    io_uring_sqe_set_data(sqe, data);
    return 0;
}

static int copy_file(int in, int out, struct io_uring *ring, off_t in_size)
{
    unsigned read_entries, write_entries;
    struct io_uring_cqe *cqe;
    off_t write_left, offset;
    int ret;
    struct io_data *data;
    write_left = in_size;
    read_entries = write_entries = offset = 0;
    while (in_size || write_left)
    {
        unsigned read_check = read_entries;
        for (int i = 0; i < QUEUE_READ_SIZE; i++)
        {
            off_t size = in_size;
            if (size > BLOCKSIZE)
                size = BLOCKSIZE;
            if (size == 0)
                break;
            if (entry_read(in, ring, size, offset))
                break;
            in_size -= size;
            offset += size;
            read_entries++;
        }

        if (read_check != read_entries)
        {
            ret = io_uring_submit(ring);
            if (ret < 0)
                break;
        }

        while (write_left)
        {
            ret = io_uring_wait_cqe(ring, &cqe);
            data = io_uring_cqe_get_data(cqe);

            if (data->read)
            {
                entry_write(out, ring, data);
                write_left -= data->first_len;
                read_entries--;
                write_entries++;
            }
            else
            {
                free(data);
                write_entries--;
            }
            io_uring_cqe_seen(ring, cqe);    
        }
    }
    for (unsigned i = 0; i < write_entries; i++)
    {
        io_uring_wait_cqe(ring, &cqe);
        data = io_uring_cqe_get_data(cqe);
        if (!data)
            break;
        free(data);
        io_uring_cqe_seen(ring, cqe);
    }
    return 0;
}

int copy(int in, int out)
{
    struct io_uring ring;
    off_t in_size;
    int ret;

    if (io_uring_queue_init(ENTRIES, &ring, 0) < 0)
        return -errno;

    if (file_size(in, &in_size) < 0)
        return -errno;
    
    ret = copy_file(in, out, &ring, in_size);

    io_uring_queue_exit(&ring);
    return ret;
}
