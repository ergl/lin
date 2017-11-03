#include <linux/errno.h>
#include <sys/syscall.h>
#include <linux/unistd.h>
#include <stdio.h>
#include <string.h>

#ifdef __i386__
#define __NR_LEDCTL 353
#else
#define __NR_LEDCTL 316
#endif

#define READ_BUF_LEN 256

long ledctl(const char* input, size_t len) {
    return (long) syscall(__NR_LEDCTL, input, len);
}

int main(int argc, char *argv[]) {
    size_t input_len;
    char buffer[READ_BUF_LEN];

    if (argc != 2) {
        return -1;
    }

    buffer = argv[1];
    input_len = strlen(&buffer);
    return ledctl(&buffer, input_len);
}