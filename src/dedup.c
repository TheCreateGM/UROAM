#define _GNU_SOURCE
#include "memopt.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <pthread.h>
#include <time.h>

#define KSM_PATH "/sys/kernel/mm/ksm/"
#define THP_PATH "/sys/kernel/mm/transparent_hugepage/"

static int write_sysfs(const char *path, const char *value) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    ssize_t len = strlen(value);
    ssize_t written = write(fd, value, len);
    close(fd);
    return (written == len) ? 0 : -1;
}

typedef struct {
    unsigned int run;
    unsigned int pages_shared;
    unsigned int pages_sharing;
    unsigned int pages_volatile;
    unsigned int full_scans;
    unsigned int pages_scanned;
    unsigned int sleep_millisecs;
    unsigned int pages_to_scan;
    unsigned int max_page_sharing;
} ksm_stats_t;

typedef struct {
    unsigned int enabled;
    unsigned int defrag;
    unsigned int khugepaged_enabled;
    unsigned int khugepaged_defrag;
} thp_stats_t;

static pthread_mutex_t dedup_lock = PTHREAD_MUTEX_INITIALIZER;
static bool dedup_running = false;
static pthread_t dedup_thread;
static unsigned int scan_batch_size = 256;
static unsigned int scan_interval_ms = 1000;

static int read_ksm_int(const char* file) {
    char path[256];
    char buf[64];
    
    snprintf(path, sizeof(path), "%s%s", KSM_PATH, file);
    
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    
    ssize_t len = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    
    if (len <= 0) return 0;
    buf[len] = '\0';
    
    return atoi(buf);
}

static int write_ksm_int(const char* file, int value) {
    char path[256];
    char buf[32];
    
    snprintf(path, sizeof(path), "%s%s", KSM_PATH, file);
    snprintf(buf, sizeof(buf), "%d", value);
    
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    
    ssize_t len = strlen(buf);
    ssize_t written = write(fd, buf, len);
    close(fd);
    
    return (written == len) ? 0 : -1;
}

static int read_thp_int(const char* path_suffix) {
    char path[256];
    char buf[64];
    
    snprintf(path, sizeof(path), "%s%s", THP_PATH, path_suffix);
    
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    
    ssize_t len = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    
    if (len <= 0) return 0;
    buf[len] = '\0';
    
    if (strncmp(buf, "[always]", 7) == 0) return 2;
    if (strncmp(buf, "[madvise]", 8) == 0) return 1;
    if (strncmp(buf, "[never]", 7) == 0) return 0;
    
    return atoi(buf);
}

int memopt_enable_ksm_dedup(void) {
    pthread_mutex_lock(&dedup_lock);
    
    if (access(KSM_PATH "run", F_OK) != 0) {
        pthread_mutex_unlock(&dedup_lock);
        return -1;
    }
    
    if (write_ksm_int("run", 1) != 0) {
        pthread_mutex_unlock(&dedup_lock);
        return -1;
    }
    
    if (write_ksm_int("sleep_millisecs", 10) != 0) {
        write_ksm_int("sleep_millisecs", 20);
    }
    
    if (write_ksm_int("pages_to_scan", scan_batch_size) != 0) {
        write_ksm_int("pages_to_scan", 256);
    }
    
    write_ksm_int("max_page_sharing", 8);
    
    pthread_mutex_unlock(&dedup_lock);
    return 0;
}

int memopt_disable_ksm_dedup(void) {
    pthread_mutex_lock(&dedup_lock);
    int ret = write_ksm_int("run", 0);
    pthread_mutex_unlock(&dedup_lock);
    return ret;
}

int memopt_set_swappiness(int value) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    return write_sysfs("/proc/sys/vm/swappiness", buf);
}

int memopt_get_ksm_stats(ksm_stats_t* stats) {
    if (!stats) return -1;
    
    stats->run = read_ksm_int("run");
    stats->pages_shared = read_ksm_int("pages_shared");
    stats->pages_sharing = read_ksm_int("pages_sharing");
    stats->pages_volatile = read_ksm_int("pages_volatile");
    stats->full_scans = read_ksm_int("full_scans");
    stats->pages_scanned = read_ksm_int("pages_scanned");
    stats->sleep_millisecs = read_ksm_int("sleep_millisecs");
    stats->pages_to_scan = read_ksm_int("pages_to_scan");
    stats->max_page_sharing = read_ksm_int("max_page_sharing");
    
    return 0;
}

int memopt_set_ksm_batch_size(unsigned int size) {
    if (size < 4) size = 4;
    if (size > 10000) size = 10000;
    
    scan_batch_size = size;
    return write_ksm_int("pages_to_scan", size);
}

int memopt_set_ksm_scan_interval(unsigned int ms) {
    if (ms < 10) ms = 10;
    if (ms > 60000) ms = 60000;
    
    scan_interval_ms = ms;
    return write_ksm_int("sleep_millisecs", ms);
}

int memopt_merge_pages(void* addr, size_t size) {
    return madvise(addr, size, MADV_MERGEABLE);
}

int memopt_unmerge_pages(void* addr, size_t size) {
    return madvise(addr, size, MADV_UNMERGEABLE);
}

int memopt_enable_hugepages_dedup(void) {
    if (access(THP_PATH "enabled", F_OK) != 0) {
        return -1;
    }
    
    int fd = open(THP_PATH "enabled", O_WRONLY);
    if (fd < 0) return -1;
    write(fd, "always\n", 7);
    close(fd);
    
    fd = open(THP_PATH "defrag", O_WRONLY);
    if (fd >= 0) {
        write(fd, "madvise\n", 8);
        close(fd);
    }
    
    return 0;
}

int memopt_disable_hugepages(void) {
    if (access(THP_PATH "enabled", F_OK) != 0) {
        return -1;
    }
    
    int fd = open(THP_PATH "enabled", O_WRONLY);
    if (fd < 0) return -1;
    write(fd, "never\n", 6);
    close(fd);
    
    return 0;
}

int memopt_get_thp_stats(thp_stats_t* stats) {
    if (!stats) return -1;
    
    stats->enabled = read_thp_int("enabled");
    stats->defrag = read_thp_int("defrag");
    stats->khugepaged_enabled = read_thp_int("khugepaged/enabled");
    stats->khugepaged_defrag = read_thp_int("khugepaged/defrag");
    
    return 0;
}

static void* dedup_monitor_thread(void* arg) {
    (void)arg;
    
    ksm_stats_t stats;
    time_t last_full_scan = 0;
    
    while (dedup_running) {
        memopt_get_ksm_stats(&stats);
        
        if (stats.run && stats.full_scans > last_full_scan) {
            if (stats.pages_sharing > stats.pages_shared * 2) {
                write_ksm_int("pages_to_scan", scan_batch_size * 2);
            } else if (stats.pages_volatile > stats.pages_shared) {
                write_ksm_int("pages_to_scan", scan_batch_size / 2);
            }
            last_full_scan = stats.full_scans;
        }
        
        usleep(scan_interval_ms * 1000);
    }
    
    return NULL;
}

int memopt_start_dedup_monitor(void) {
    if (dedup_running) return 0;
    
    pthread_mutex_lock(&dedup_lock);
    dedup_running = true;
    pthread_create(&dedup_thread, NULL, dedup_monitor_thread, NULL);
    pthread_mutex_unlock(&dedup_lock);
    
    return 0;
}

int memopt_stop_dedup_monitor(void) {
    if (!dedup_running) return 0;
    
    pthread_mutex_lock(&dedup_lock);
    dedup_running = false;
    pthread_mutex_unlock(&dedup_lock);
    
    pthread_join(dedup_thread, NULL);
    
    return 0;
}

int memopt_aggressive_dedup(void) {
    memopt_enable_ksm_dedup();
    write_ksm_int("pages_to_scan", 5000);
    write_ksm_int("sleep_millisecs", 1);

    memopt_enable_hugepages_dedup();
    
    return 0;
}

int memopt_conservative_dedup(void) {
    write_ksm_int("pages_to_scan", 100);
    write_ksm_int("sleep_millisecs", 1000);
    write_ksm_int("max_page_sharing", 4);

    return 0;
}
int memopt_enable_ksm(void) {
    return memopt_enable_ksm_dedup();
}

int memopt_disable_ksm(void) {
    return memopt_disable_ksm_dedup();
}

int memopt_enable_hugepages(void) {
    return memopt_enable_hugepages_dedup();
}
