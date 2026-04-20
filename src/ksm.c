#define _GNU_SOURCE
#include "memopt.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

#define KSM_PATH "/sys/kernel/mm/ksm/"
#define ZSWAP_PATH "/sys/module/zswap/parameters/"

static int write_sysfs(const char* path, const char* value) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    
    ssize_t len = write(fd, value, strlen(value));
    close(fd);
    
    if (len < 0) return -1;
    return 0;
}

int memopt_enable_ksm(void) {
    if (access(KSM_PATH "run", F_OK) != 0) {
        return -1;
    }

    if (write_sysfs(KSM_PATH "run", "1") != 0) return -1;
    if (write_sysfs(KSM_PATH "sleep_millisecs", "20") != 0) return -1;
    if (write_sysfs(KSM_PATH "pages_to_scan", "1000") != 0) return -1;
    
    return 0;
}

int memopt_disable_ksm(void) {
    return write_sysfs(KSM_PATH "run", "0");
}

int memopt_set_swappiness(int value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    return write_sysfs("/proc/sys/vm/swappiness", buf);
}

int memopt_tune_zswap(void) {
    if (access(ZSWAP_PATH "enabled", F_OK) != 0) {
        return -1;
    }

    if (write_sysfs(ZSWAP_PATH "enabled", "Y") != 0) return -1;
    if (write_sysfs(ZSWAP_PATH "max_pool_percent", "25") != 0) return -1;
    if (write_sysfs(ZSWAP_PATH "compressor", "zstd") != 0) return -1;
    if (write_sysfs(ZSWAP_PATH "zpool", "z3fold") != 0) return -1;
    
    return 0;
}

int memopt_enable_hugepages(void) {
    if (access("/sys/kernel/mm/transparent_hugepage/enabled", F_OK) != 0) {
        return -1;
    }
    return write_sysfs("/sys/kernel/mm/transparent_hugepage/enabled", "always");
}
