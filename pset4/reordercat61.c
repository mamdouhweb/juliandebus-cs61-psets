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

    size_t inf_size = io61_filesize(inf);
    if ((ssize_t) inf_size < 0) {
        fprintf(stderr, "reordercat61: input file is not seekable\n");
        exit(1);
    }

    io61_file *outf = io61_fdopen(STDOUT_FILENO, O_WRONLY);
    if (io61_seek(outf, 0) < 0) {
        fprintf(stderr, "reordercat61: output file is not seekable\n");
        exit(1);
    }

    size_t npermute = inf_size / block_size;
    if (npermute > (30 << 20)) {
        fprintf(stderr, "reordercat61: file too large\n");
        exit(1);
    }
    size_t *permute = (size_t *) malloc(sizeof(size_t) * npermute);
    for (size_t i = 0; i < npermute; ++i)
        permute[i] = i;

    while (npermute) {
        size_t which_permute = random() % npermute;
        size_t pos = permute[which_permute] * block_size;
        permute[which_permute] = permute[npermute - 1];
        --npermute;

        ssize_t amount = block_size;
        if (pos + amount > inf_size)
            amount = inf_size - pos;

        io61_seek(inf, pos);
        amount = io61_read(inf, buf, amount);
        if (amount <= 0)
            break;
        io61_seek(outf, pos);
        io61_write(outf, buf, amount);
    }

    io61_close(inf);
    io61_close(outf);
}
