#include <sys/types.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#define BUCKETS 10
#define MODULE_PATH "/proc/list/"
#define DEFAULT_LIST_NAME "default"
#define CONTROL_FILE "/proc/list/control"

// https://stackoverflow.com/a/8465083
char* concat(const char *s1, const char *s2) {
    const size_t len1 = strlen(s1);
    const size_t len2 = strlen(s2);
    char *result = malloc(len1 + len2 + 1);
    memcpy(result, s1, len1);
    memcpy(result + len1, s2, len2+1);
    return result;
}

int create_list(char* name) {
    FILE* fd;
    if ((fd = fopen(CONTROL_FILE, "w+")) == NULL) {
        return -1;
    }

    fprintf(fd, "create %s\n", name);
    return fclose(fd);
}

int add_to_list(char* list_path, int val) {
    FILE* fd;
    if ((fd = fopen(list_path, "w+")) == NULL) {
        return -1;
    }

    fprintf(fd, "add %d\n", val);
    return fclose(fd);
}

int delete_list(char* name) {
    FILE* fd;
    if ((fd = fopen(CONTROL_FILE, "w+")) == NULL) {
        return -1;
    }

    fprintf(fd, "delete %s\n", name);
    return fclose(fd);
}

int create_buckets(void) {
    int ret = 0;
    int i;
    int sz;
    DIR *dir;
    if ((dir = opendir(MODULE_PATH)) == NULL) {
        printf("Couldn't open proc folder\n");
        return -1;
    }

    // We don't care about the default entry
    delete_list(DEFAULT_LIST_NAME);

    for (i = 0; i < BUCKETS; i++) {
        sz = snprintf(NULL, 0, "%d", i);
        char list_name[sz + 1];
        snprintf(list_name, sizeof(list_name), "%d", i);
        // We dont' care if the list doesn't exist
        delete_list(list_name);

        if (create_list(list_name) == -1) {
            ret = -1;
            break;
        }
    }

    closedir(dir);

    return ret;
}

int delete_buckets(void) {
    int ret = 0;
    int i;
    int sz;
    DIR *dir;
    if ((dir = opendir(MODULE_PATH)) == NULL) {
        printf("Couldn't open proc folder\n");
        return -1;
    }

    for (i = 0; i < BUCKETS; i++) {
        sz = snprintf(NULL, 0, "%d", i);
        char list_name[sz + 1];
        snprintf(list_name, sizeof(list_name), "%d", i);
        if (delete_list(list_name) == -1) {
            ret = -1;
            break;
        }
    }

    closedir(dir);

    return ret;
}

// https://stackoverflow.com/a/7666577
unsigned long hash(unsigned char *str) {
    unsigned long hash = 5381;
    int c;

    while (c = *str++)
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}

int bucket_for_key(char* key) {
    unsigned long h = hash(key);
    return h % BUCKETS;
}

void write_into_list(char* list_name, int value) {
    char* full_path = concat(MODULE_PATH, list_name);
    add_to_list(full_path, value);
    free(full_path);
}

int put(char* key, int value) {
    int bucket = bucket_for_key(key);
    char s_bucket[1];
    sprintf(s_bucket, "%d", bucket);
    write_into_list(s_bucket, value);
}

int main(int argc, char** argv) {
    // if (create_buckets() == -1) {
    //     printf("Couldn't create buckets\n");
    // }

    // if (argc != 3) {
    //     printf("Usage: %s <key> <value>\n", argv[0]);
    //     return -1;
    // }

    // char* command = argv[1];
    // char* key = argv[2];
    // int value = atoi(argv[3]);

    // printf("%d\n", bucket_for_key(key));
    // put(key, value);

    // if (delete_buckets() == -1) {
    //     printf("Couldn't delete buckets\n");
    // }

    int opt;
    char* key;
    int value;
    int flags = 0;

    while ((opt = getopt(argc, argv, "ntk:v:")) != -1) {
        switch (opt) {
            case 'n':
                printf("Setup\n");
                break;
            case 't':
                printf("Tear-down\n");
                break;
            case 'k':
                key = optarg;
                printf("Key: %s\n", key);
                break;
            case 'v':
                value = atoi(optarg);
                printf("Value: %d\n", value);
                break;
            default:
                fprintf(stderr, "Usage %s [-nt] [-k <key>] [-v <value]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    exit(EXIT_SUCCESS);
}