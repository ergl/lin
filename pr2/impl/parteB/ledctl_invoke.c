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

long ledctl(unsigned int lednum) {
    return (long) syscall(__NR_LEDCTL, lednum);
}

int get_user_num(const char* buffer, int* container) {
    const char* format = "0x%i";
    return sscanf(buffer, format, container);
}

int main(int argc, char *argv[]) {
    int data;

    if (argc != 2) {
        return -1;
    }

    if (get_user_num(argv[1], &data)) {
        return ledctl(data);
    }

    return 0;
}
