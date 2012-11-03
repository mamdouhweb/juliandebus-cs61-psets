#include "io61.h"

int main(int argc, char **argv) {
    size_t block_size = 4096;
    if (argc >= 3 && strcmp(argv[1], "-b") == 0) {
        block_size = strtoul(argv[2], 0, 0);
        argc -= 2, argv += 2;
    }
    assert(block_size > 0);
    char *buf = malloc(block_size);

    const char *in_filename = argc >= 2 ? argv[1] : NULL;
    io61_file *inf = io61_open_check(in_filename, O_RDONLY);
    io61_file *outf = io61_fdopen(STDOUT_FILENO, O_WRONLY);

    while (1) {
        ssize_t amount = io61_read(inf, buf, block_size);
        if (amount <= 0)
            break;
        io61_write(outf, buf, amount);
    }

    io61_close(inf);
    io61_close(outf);
}
