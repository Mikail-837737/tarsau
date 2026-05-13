#include <stdio.h>
#include <string.h>

#include "tarsau.h"

static void print_usage(void)
{
    printf("Kullanim:\n");
    printf("  tarsau -b dosya1 dosya2 ... [-o arsiv.sau]\n");
    printf("  tarsau -a arsiv.sau [hedef_dizin]\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "-b") == 0) {
        return build_archive(argc - 2, argv + 2);
    }

    if (strcmp(argv[1], "-a") == 0) {
        return extract_archive(argc - 2, argv + 2);
    }

    print_usage();
    return 1;
}
