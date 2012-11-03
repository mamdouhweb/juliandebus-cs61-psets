#include "io61.h"

int main(int argc, char **argv) {
    size_t block_size = 1;
    size_t stride = 1024;

    while (argc >= 3) {
        if (strcmp(argv[1], "-b") == 0) {
            block_size = strtoul(argv[2], 0, 0);
            argc -= 2, argv += 2;
        } else if (strcmp(argv[1], "-s") == 0) {
            stride = strtoul(argv[2], 0, 0);
            argc -= 2, argv += 2;
        } else
            break;
    }

    assert(block_size > 0);
    char *buf = malloc(block_size);

    const char *in_filename = argc >= 2 ? argv[1] : NULL;
    io61_file *inf = io61_open_check(in_filename, O_RDONLY);

    size_t inf_size = io61_filesize(inf);
    if ((ssize_t) inf_size < 0) {
        fprintf(stderr, "ostridecat61: input file is not seekable\n");
        exit(1);
    }

    io61_file *outf = io61_fdopen(STDOUT_FILENO, O_WRONLY);
    if (io61_seek(outf, 0) < 0) {
        fprintf(stderr, "ostridecat61: output file is not seekable\n");
        exit(1);
    }

    size_t pos = 0, written = 0;
    while (written < inf_size) {
        ssize_t amount = block_size;
        if (pos + block_size > inf_size)
            amount = inf_size - pos;

        amount = io61_read(inf, buf, amount);
        if (amount <= 0)
            break;
        io61_write(outf, buf, amount);
        written += amount;

        pos += stride;
        if (pos >= inf_size) {
            pos = (pos % stride) + block_size;
            if (pos + block_size > stride)
                block_size = stride - pos;
        }
        int r = io61_seek(outf, pos);
        assert(r >= 0);
    }

    io61_close(inf);
    io61_close(outf);
}
