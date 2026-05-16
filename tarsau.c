#define _POSIX_C_SOURCE 200809L

#include "tarsau.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_FILES 32
#define MAX_TOTAL_SIZE (200LL * 1024LL * 1024LL)
#define HEADER_SIZE 10
#define COPY_BUFFER_SIZE 8192

#ifndef NAME_MAX
#define NAME_MAX 255
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    char name[NAME_MAX + 1];
    char source_path[PATH_MAX];
    dev_t device;
    ino_t inode;
    mode_t permissions;
    off_t size;
} FileInfo;

typedef struct {
    char name[NAME_MAX + 1];
    mode_t permissions;
    long long size;
} ArchiveEntry;

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int has_sau_extension(const char *name)
{
    size_t len = strlen(name);
    return len > 4 && strcmp(name + len - 4, ".sau") == 0;
}

static int write_all(int fd, const void *buffer, size_t count)
{
    const char *ptr = (const char *)buffer;
    while (count > 0) {
        ssize_t written = write(fd, ptr, count);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        ptr += written;
        count -= (size_t)written;
    }
    return 0;
}

static int read_all(int fd, void *buffer, size_t count)
{
    char *ptr = (char *)buffer;
    while (count > 0) {
        ssize_t bytes_read = read(fd, ptr, count);
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (bytes_read == 0) {
            return -1;
        }
        ptr += bytes_read;
        count -= (size_t)bytes_read;
    }
    return 0;
}

static int is_ascii_text_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    unsigned char buffer[COPY_BUFFER_SIZE];

    if (fd < 0) {
        return 0;
    }

    while (1) {
        ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd);
            return 0;
        }
        if (bytes_read == 0) {
            break;
        }

        for (ssize_t i = 0; i < bytes_read; i++) {
            unsigned char c = buffer[i];
            if (c > 127 || (c < 32 && c != '\n' && c != '\r' && c != '\t')) {
                close(fd);
                return 0;
            }
        }
    }

    close(fd);
    return 1;
}

static int copy_exact_bytes(int in_fd, int out_fd, long long size)
{
    char buffer[COPY_BUFFER_SIZE];
    long long remaining = size;

    while (remaining > 0) {
        size_t wanted = remaining > (long long)sizeof(buffer)
                            ? sizeof(buffer)
                            : (size_t)remaining;
        ssize_t bytes_read = read(in_fd, buffer, wanted);

        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (bytes_read == 0) {
            return -1;
        }
        if (write_all(out_fd, buffer, (size_t)bytes_read) != 0) {
            return -1;
        }

        remaining -= bytes_read;
    }

    return 0;
}

static int collect_file_info(char **files, int file_count, FileInfo *infos)
{
    long long total_size = 0;

    if (file_count <= 0 || file_count > MAX_FILES) {
        fprintf(stderr, "Giris dosyasi sayisi 1 ile 32 arasinda olmalidir.\n");
        return -1;
    }

    for (int i = 0; i < file_count; i++) {
        struct stat st;
        const char *name = base_name(files[i]);
        int needed;

        if (name[0] == '\0' || strchr(name, ',') || strchr(name, '|')) {
            printf("%s giriş dosyasının formatı uyumsuzdur!\n", files[i]);
            return -1;
        }

        if (strlen(name) > NAME_MAX || strlen(files[i]) >= PATH_MAX) {
            printf("%s giriş dosyasının formatı uyumsuzdur!\n", files[i]);
            return -1;
        }

        for (int j = 0; j < i; j++) {
            if (strcmp(infos[j].name, name) == 0) {
                printf("%s giriş dosyasının formatı uyumsuzdur!\n", files[i]);
                return -1;
            }
        }

        if (stat(files[i], &st) != 0 || !S_ISREG(st.st_mode)) {
            printf("%s giriş dosyasının formatı uyumsuzdur!\n", files[i]);
            return -1;
        }

        if (!is_ascii_text_file(files[i])) {
            printf("%s giriş dosyasının formatı uyumsuzdur!\n", files[i]);
            return -1;
        }

        total_size += (long long)st.st_size;
        if (total_size > MAX_TOTAL_SIZE) {
            fprintf(stderr, "Giris dosyalarinin toplam boyutu 200 MB'i gecemez.\n");
            return -1;
        }

        needed = snprintf(infos[i].name, sizeof(infos[i].name), "%s", name);
        if (needed < 0 || (size_t)needed >= sizeof(infos[i].name)) {
            printf("%s giriş dosyasının formatı uyumsuzdur!\n", files[i]);
            return -1;
        }

        needed = snprintf(infos[i].source_path, sizeof(infos[i].source_path), "%s", files[i]);
        if (needed < 0 || (size_t)needed >= sizeof(infos[i].source_path)) {
            printf("%s giriş dosyasının formatı uyumsuzdur!\n", files[i]);
            return -1;
        }

        infos[i].permissions = st.st_mode & 0777;
        infos[i].size = st.st_size;
        infos[i].device = st.st_dev;
        infos[i].inode = st.st_ino;
    }

    return 0;
}

static int archive_matches_input(const char *archive_name, FileInfo *infos, int file_count)
{
    struct stat archive_stat;

    if (stat(archive_name, &archive_stat) != 0) {
        return 0;
    }

    for (int i = 0; i < file_count; i++) {
        if (archive_stat.st_dev == infos[i].device &&
            archive_stat.st_ino == infos[i].inode) {
            return 1;
        }
    }

    return 0;
}

static char *create_metadata(FileInfo *infos, int file_count, size_t *metadata_size)
{
    size_t capacity = 256;
    size_t used = 0;
    char *metadata = malloc(capacity);

    if (!metadata) {
        return NULL;
    }
    metadata[0] = '\0';

    for (int i = 0; i < file_count; i++) {
        char record[NAME_MAX + 64];
        int needed = snprintf(record, sizeof(record), "|%s,%03o,%lld|",
                              infos[i].name,
                              (unsigned int)infos[i].permissions,
                              (long long)infos[i].size);

        if (needed < 0 || (size_t)needed >= sizeof(record)) {
            free(metadata);
            return NULL;
        }

        if (used + (size_t)needed + 1 > capacity) {
            while (used + (size_t)needed + 1 > capacity) {
                capacity *= 2;
            }
            char *grown = realloc(metadata, capacity);
            if (!grown) {
                free(metadata);
                return NULL;
            }
            metadata = grown;
        }

        memcpy(metadata + used, record, (size_t)needed);
        used += (size_t)needed;
        metadata[used] = '\0';
    }

    *metadata_size = used;
    return metadata;
}

static int write_archive(const char *archive_name, FileInfo *infos, int file_count)
{
    size_t metadata_size = 0;
    char *metadata = create_metadata(infos, file_count, &metadata_size);
    char header[HEADER_SIZE + 1];
    int archive_fd;

    if (!metadata) {
        fprintf(stderr, "Arsiv bilgisi olusturulamadi.\n");
        return -1;
    }

    if (metadata_size > 9999999999ULL) {
        free(metadata);
        fprintf(stderr, "Arsiv organizasyon bolumu cok buyuk.\n");
        return -1;
    }

    snprintf(header, sizeof(header), "%010zu", metadata_size);

    archive_fd = open(archive_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (archive_fd < 0) {
        free(metadata);
        perror("Arsiv dosyasi acilamadi");
        return -1;
    }

    if (write_all(archive_fd, header, HEADER_SIZE) != 0 ||
        write_all(archive_fd, metadata, metadata_size) != 0) {
        free(metadata);
        close(archive_fd);
        perror("Arsiv dosyasina yazilamadi");
        return -1;
    }

    free(metadata);

    for (int i = 0; i < file_count; i++) {
        int in_fd = open(infos[i].source_path, O_RDONLY);
        if (in_fd < 0) {
            close(archive_fd);
            perror("Giris dosyasi acilamadi");
            return -1;
        }

        if (copy_exact_bytes(in_fd, archive_fd, (long long)infos[i].size) != 0) {
            close(in_fd);
            close(archive_fd);
            perror("Dosya arsive kopyalanamadi");
            return -1;
        }

        close(in_fd);
    }

    if (close(archive_fd) != 0) {
        perror("Arsiv dosyasi kapatilamadi");
        return -1;
    }

    return 0;
}

int build_archive(int argc, char **argv)
{
    char *files[MAX_FILES + 1];
    FileInfo infos[MAX_FILES];
    const char *archive_name = "a.sau";
    int file_count = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "-o parametresinden sonra arsiv dosyasi verilmelidir.\n");
                return 1;
            }
            archive_name = argv[++i];
            continue;
        }

        if (file_count >= MAX_FILES) {
            fprintf(stderr, "Giris dosyasi sayisi en fazla 32 olabilir.\n");
            return 1;
        }
        files[file_count++] = argv[i];
    }

    if (collect_file_info(files, file_count, infos) != 0) {
        return 1;
    }

    if (!has_sau_extension(archive_name)) {
        fprintf(stderr, "Arsiv dosyasinin uzantisi .sau olmalidir.\n");
        return 1;
    }

    if (archive_matches_input(archive_name, infos, file_count)) {
        fprintf(stderr, "Arsiv dosyasi giris dosyalarindan biri olamaz.\n");
        return 1;
    }

    if (write_archive(archive_name, infos, file_count) != 0) {
        return 1;
    }

    printf("Dosyalar birleştirildi.\n");
    return 0;
}

static int parse_nonnegative_number(const char *text, long long *value)
{
    long long result = 0;

    if (!text || text[0] == '\0') {
        return -1;
    }

    for (size_t i = 0; text[i]; i++) {
        if (!isdigit((unsigned char)text[i])) {
            return -1;
        }
        result = result * 10 + (text[i] - '0');
        if (result > MAX_TOTAL_SIZE) {
            return -1;
        }
    }

    *value = result;
    return 0;
}

static int parse_permission(const char *text, mode_t *permissions)
{
    mode_t result = 0;

    if (!text || strlen(text) != 3) {
        return -1;
    }

    for (size_t i = 0; i < 3; i++) {
        if (text[i] < '0' || text[i] > '7') {
            return -1;
        }
        result = (result << 3) | (mode_t)(text[i] - '0');
    }

    *permissions = result;
    return 0;
}

static int parse_metadata(char *metadata, size_t metadata_size,
                          ArchiveEntry **entries_out, int *entry_count_out)
{
    ArchiveEntry *entries = calloc(MAX_FILES, sizeof(ArchiveEntry));
    int count = 0;
    size_t pos = 0;

    if (!entries) {
        return -1;
    }

    while (pos < metadata_size) {
        char *record_start;
        char *record_end;
        char *comma1;
        char *comma2;
        long long file_size;
        mode_t permissions;
        size_t name_len;

        if (metadata[pos] != '|') {
            free(entries);
            return -1;
        }
        pos++;
        record_start = metadata + pos;

        record_end = memchr(record_start, '|', metadata_size - pos);
        if (!record_end) {
            free(entries);
            return -1;
        }

        *record_end = '\0';
        comma1 = strchr(record_start, ',');
        comma2 = comma1 ? strchr(comma1 + 1, ',') : NULL;
        if (!comma1 || !comma2 || strchr(comma2 + 1, ',')) {
            free(entries);
            return -1;
        }

        *comma1 = '\0';
        *comma2 = '\0';

        name_len = strlen(record_start);
        if (count >= MAX_FILES || name_len == 0 || name_len > NAME_MAX ||
            strchr(record_start, '/') || strchr(record_start, '\\') ||
            strcmp(record_start, ".") == 0 || strcmp(record_start, "..") == 0) {
            free(entries);
            return -1;
        }

        for (int i = 0; i < count; i++) {
            if (strcmp(entries[i].name, record_start) == 0) {
                free(entries);
                return -1;
            }
        }

        if (parse_permission(comma1 + 1, &permissions) != 0 ||
            parse_nonnegative_number(comma2 + 1, &file_size) != 0) {
            free(entries);
            return -1;
        }

        snprintf(entries[count].name, sizeof(entries[count].name), "%s", record_start);
        entries[count].permissions = permissions;
        entries[count].size = file_size;
        count++;

        pos = (size_t)(record_end - metadata) + 1;
    }

    if (count == 0) {
        free(entries);
        return -1;
    }

    *entries_out = entries;
    *entry_count_out = count;
    return 0;
}

static int make_directory_if_needed(const char *directory)
{
    struct stat st;

    if (!directory || directory[0] == '\0' || strcmp(directory, ".") == 0) {
        return 0;
    }

    if (stat(directory, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }

    if (mkdir(directory, 0755) != 0) {
        return -1;
    }

    return 0;
}

static int create_output_path(char *buffer, size_t buffer_size,
                              const char *directory, const char *name)
{
    int needed;

    if (!directory || directory[0] == '\0' || strcmp(directory, ".") == 0) {
        needed = snprintf(buffer, buffer_size, "%s", name);
    } else {
        size_t len = strlen(directory);
        if (directory[len - 1] == '/') {
            needed = snprintf(buffer, buffer_size, "%s%s", directory, name);
        } else {
            needed = snprintf(buffer, buffer_size, "%s/%s", directory, name);
        }
    }

    return needed < 0 || (size_t)needed >= buffer_size ? -1 : 0;
}

static int archive_corrupt(void)
{
    printf("Arşiv dosyası uygunsuz veya bozuk!\n");
    return 1;
}

int extract_archive(int argc, char **argv)
{
    const char *archive_name;
    const char *output_directory = ".";
    char header[HEADER_SIZE + 1];
    char *metadata = NULL;
    ArchiveEntry *entries = NULL;
    int entry_count = 0;
    int archive_fd;
    long long total_size = 0;
    long long metadata_size = 0;

    if (argc < 1 || argc > 2) {
        return archive_corrupt();
    }

    archive_name = argv[0];
    if (!has_sau_extension(archive_name)) {
        return archive_corrupt();
    }

    if (argc == 2) {
        output_directory = argv[1];
    }

    archive_fd = open(archive_name, O_RDONLY);
    if (archive_fd < 0) {
        return archive_corrupt();
    }

    if (read_all(archive_fd, header, HEADER_SIZE) != 0) {
        close(archive_fd);
        return archive_corrupt();
    }
    header[HEADER_SIZE] = '\0';

    if (parse_nonnegative_number(header, &metadata_size) != 0 || metadata_size <= 0) {
        close(archive_fd);
        return archive_corrupt();
    }

    metadata = malloc((size_t)metadata_size + 1);
    if (!metadata) {
        close(archive_fd);
        return archive_corrupt();
    }

    if (read_all(archive_fd, metadata, (size_t)metadata_size) != 0) {
        free(metadata);
        close(archive_fd);
        return archive_corrupt();
    }
    metadata[metadata_size] = '\0';

    if (parse_metadata(metadata, (size_t)metadata_size, &entries, &entry_count) != 0) {
        free(metadata);
        close(archive_fd);
        return archive_corrupt();
    }

    for (int i = 0; i < entry_count; i++) {
        total_size += entries[i].size;
        if (total_size > MAX_TOTAL_SIZE) {
            free(entries);
            free(metadata);
            close(archive_fd);
            return archive_corrupt();
        }
    }

    if (make_directory_if_needed(output_directory) != 0) {
        free(entries);
        free(metadata);
        close(archive_fd);
        return archive_corrupt();
    }

    for (int i = 0; i < entry_count; i++) {
        char output_path[PATH_MAX];
        int out_fd;

        if (create_output_path(output_path, sizeof(output_path),
                               output_directory, entries[i].name) != 0) {
            free(entries);
            free(metadata);
            close(archive_fd);
            return archive_corrupt();
        }

        out_fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, entries[i].permissions);
        if (out_fd < 0) {
            free(entries);
            free(metadata);
            close(archive_fd);
            return archive_corrupt();
        }

        if (copy_exact_bytes(archive_fd, out_fd, entries[i].size) != 0) {
            close(out_fd);
            free(entries);
            free(metadata);
            close(archive_fd);
            return archive_corrupt();
        }

        if (fchmod(out_fd, entries[i].permissions) != 0) {
            close(out_fd);
            free(entries);
            free(metadata);
            close(archive_fd);
            return archive_corrupt();
        }

        close(out_fd);
    }

    free(entries);
    free(metadata);

    if (close(archive_fd) != 0) {
        return archive_corrupt();
    }

    if (strcmp(output_directory, ".") == 0) {
        printf("Geçerli dizinde dosyalar açıldı.\n");
    } else {
        printf("%s dizininde dosyalar açıldı.\n", output_directory);
    }

    return 0;
}
