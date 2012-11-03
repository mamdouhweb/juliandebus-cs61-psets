#include "io61.h"

int main(int argc, char **argv) {
    size_t block_size = 4096;
    srandom(83419);

    while (argc >= 3) {
        if (strcmp(argv[1], "-b") == 0) {
            block_size = strtoul(argv[2], 0, 0);
            argc -= 2, argv += 2;
        } else if (strcmp(argv[1], "-s") == 0) {
            srandom(strtoul(argv[2], 0, 0));
            argc -= 2, argv += 2;
        } else
            break;
    }

    assert(block_size > 0);
    char *buf = malloc(block_size);

    const char *in_filename = argc >= 2 ? argv[1] : NULL;
    io61_file *inf = io61_open_check(in_filename, O_RDONLY);
    io61_file *outf = io61_fdopen(STDOUT_FILENO, O_WRONLY);

    while (1) {
        size_t m = random() % block_size;
        ssize_t amount = io61_read(inf, buf, m ? m : 1);
        if (amount <= 0)
            break;
        io61_write(outf, buf, amount);
    }

    io61_close(inf);
    io61_close(outf);
}
