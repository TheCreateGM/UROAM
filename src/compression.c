#define _GNU_SOURCE
#include "memopt.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#define ZRAM_PATH "/sys/block/zram"
#define ZSWAP_PATH "/sys/module/zswap/parameters"
#define SYS_ZRAM_PATH "/sys/devices/virtual/block/zram"

static int write_sysfs(const char* path, const char* value) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    
    ssize_t len = strlen(value);
    ssize_t written = write(fd, value, len);
    close(fd);
    
    return (written == len) ? 0 : -1;
}

static int write_sysfs_int(const char* path, int value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    return write_sysfs(path, buf);
}

static int read_sysfs(const char* path, char* buf, size_t size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    
    ssize_t len = read(fd, buf, size - 1);
    close(fd);
    
    if (len > 0) {
        buf[len] = '\0';
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) {
            buf[--len] = '\0';
        }
    }
    
    return (len > 0) ? 0 : -1;
}

static int probe_zram_device(char* name, size_t max_len) {
    if (access(SYS_ZRAM_PATH, F_OK) != 0) {
        return -1;
    }
    
    strncpy(name, "zram0", max_len - 1);
    name[max_len - 1] = '\0';
    return 0;
}

typedef struct {
    int enabled;
    int num_devices;
    size_t disk_size;
    char compressor[32];
    size_t orig_data_size;
    size_t compr_data_size;
    size_t mem_used;
    unsigned long num_migrated;
    unsigned long num_migrate_failed;
} zram_stats_t;

static int get_zram_stats(zram_stats_t* stats) {
    char zram_name[64];
    char buf[128];
    int ret;

    ret = probe_zram_device(zram_name, sizeof(zram_name));
    if (ret != 0) {
        return -1;
    }
    
    memset(stats, 0, sizeof(*stats));
    stats->enabled = 0;
    stats->num_devices = 1;
    
    char base[256];
    snprintf(base, sizeof(base), "%s/%s", SYS_ZRAM_PATH, zram_name);
    
    if (read_sysfs(base, buf, sizeof(buf)) == 0) {
        if (read_sysfs("enabled", buf, sizeof(buf)) == 0) {
            stats->enabled = (strcmp(buf, "1") == 0 || strcmp(buf, "comp") == 0);
        }
        
        snprintf(base, sizeof(base), "%s/%s/disksize", SYS_ZRAM_PATH, zram_name);
        if (read_sysfs(base, buf, sizeof(buf)) == 0) {
            stats->disk_size = strtoull(buf, NULL, 0);
        }
        
        snprintf(base, sizeof(base), "%s/%s/compressor", SYS_ZRAM_PATH, zram_name);
        if (read_sysfs(base, buf, sizeof(buf)) == 0) {
            strncpy(stats->compressor, buf, sizeof(stats->compressor) - 1);
        }
        
        snprintf(base, sizeof(base), "%s/%s/orig_data_size", SYS_ZRAM_PATH, zram_name);
        if (read_sysfs(base, buf, sizeof(buf)) == 0) {
            stats->orig_data_size = strtoull(buf, NULL, 0);
        }
        
        snprintf(base, sizeof(base), "%s/%s/compr_data_size", SYS_ZRAM_PATH, zram_name);
        if (read_sysfs(base, buf, sizeof(buf)) == 0) {
            stats->compr_data_size = strtoull(buf, NULL, 0);
        }
        
        snprintf(base, sizeof(base), "%s/%s/mem_used_max", SYS_ZRAM_PATH, zram_name);
        if (read_sysfs(base, buf, sizeof(buf)) == 0) {
            stats->mem_used = strtoull(buf, NULL, 0);
        }
    }
    
    return 0;
}

int memopt_enable_zram(void) {
    char zram_name[64];
    char path[256];
    int ret;

    ret = probe_zram_device(zram_name, sizeof(zram_name));
    if (ret != 0) {
        fprintf(stderr, "uroam: no zram device found\n");
        return -1;
    }
    
    snprintf(path, sizeof(path), "%s/%s/disksize", SYS_ZRAM_PATH, zram_name);
    if (write_sysfs(path, "4G") != 0) {
        return -1;
    }
    
    snprintf(path, sizeof(path), "%s/%s/comp", SYS_ZRAM_PATH, zram_name);
    if (write_sysfs(path, "zstd") != 0) {
        write_sysfs(path, "lzo");
    }
    
    snprintf(path, sizeof(path), "%s/%s/algorithm", SYS_ZRAM_PATH, zram_name);
    if (write_sysfs(path, "lz4") != 0) {
        return -1;
    }
    
    snprintf(path, sizeof(path), "%s/%s/disksize", SYS_ZRAM_PATH, zram_name);
    char buf[64];
    if (read_sysfs(path, buf, sizeof(buf)) == 0) {
        if (strcmp(buf, "0") == 0) {
            write_sysfs(path, "4G");
        }
    }
    
    snprintf(path, sizeof(path), "%s/%s/reset", SYS_ZRAM_PATH, zram_name);
    write_sysfs(path, "1");
    
    return 0;
}

int memopt_disable_zram(void) {
    char zram_name[64];
    char path[256];
    int ret;

    ret = probe_zram_device(zram_name, sizeof(zram_name));
    if (ret != 0) {
        return -1;
    }
    
    snprintf(path, sizeof(path), "%s/%s/reset", SYS_ZRAM_PATH, zram_name);
    return write_sysfs(path, "1");
}

int memopt_set_zram_size(const char* size_str) {
    char zram_name[64];
    char path[256];
    int ret;

    ret = probe_zram_device(zram_name, sizeof(zram_name));
    if (ret != 0) {
        return -1;
    }
    
    snprintf(path, sizeof(path), "%s/%s/disksize", SYS_ZRAM_PATH, zram_name);
    return write_sysfs(path, size_str);
}

int memopt_set_zram_compressor(const char* comp) {
    char zram_name[64];
    char path[256];
    int ret;

    ret = probe_zram_device(zram_name, sizeof(zram_name));
    if (ret != 0) {
        return -1;
    }
    
    snprintf(path, sizeof(path), "%s/%s/comp", SYS_ZRAM_PATH, zram_name);
    return write_sysfs(path, comp);
}

int memopt_get_zram_compression_ratio(float* ratio) {
    zram_stats_t stats;
    
    if (get_zram_stats(&stats) != 0) {
        return -1;
    }
    
    if (stats.orig_data_size == 0) {
        *ratio = 1.0f;
    } else {
        *ratio = (float)stats.orig_data_size / (float)(stats.compr_data_size + 1);
    }
    
    return 0;
}

typedef struct {
    int enabled;
    int max_pool_percent;
    char compressor[32];
    char zpool[32];
    int same_filled_pages_enabled;
} zswap_stats_t;

static int get_zswap_stats(zswap_stats_t* stats) {
    char buf[64];
    
    memset(stats, 0, sizeof(*stats));
    
    if (access(ZSWAP_PATH, F_OK) != 0) {
        return -1;
    }
    
    if (read_sysfs("enabled", buf, sizeof(buf)) == 0) {
        stats->enabled = (strcmp(buf, "Y") == 0 || strcmp(buf, "1") == 0);
    }
    
    if (read_sysfs("max_pool_percent", buf, sizeof(buf)) == 0) {
        stats->max_pool_percent = atoi(buf);
    }
    
    if (read_sysfs("compressor", buf, sizeof(buf)) == 0) {
        strncpy(stats->compressor, buf, sizeof(stats->compressor) - 1);
    }
    
    if (read_sysfs("zpool", buf, sizeof(buf)) == 0) {
        strncpy(stats->zpool, buf, sizeof(stats->zpool) - 1);
    }
    
    return 0;
}

int memopt_tune_zswap(void) {
    if (access(ZSWAP_PATH, F_OK) != 0) {
        return -1;
    }
    
    if (write_sysfs_int("enabled", 1) != 0) {
        write_sysfs(ZSWAP_PATH "/enabled", "Y");
    }
    
    if (write_sysfs_int("max_pool_percent", 25) != 0) {
        write_sysfs_int(ZSWAP_PATH "/max_pool_percent", 25);
    }
    
    if (write_sysfs(ZSWAP_PATH "/compressor", "zstd") != 0) {
        write_sysfs(ZSWAP_PATH "/compressor", "lz4");
    }
    
    if (write_sysfs(ZSWAP_PATH "/zpool", "z3fold") != 0) {
        write_sysfs(ZSWAP_PATH "/zpool", "zbud");
    }
    
    return memopt_enable_zram();
}

int memopt_set_zswap_compressor(const char* comp) {
    if (access(ZSWAP_PATH, F_OK) != 0) {
        return -1;
    }
    return write_sysfs(ZSWAP_PATH "/compressor", comp);
}

int memopt_set_zswap_pool_percent(int percent) {
    if (access(ZSWAP_PATH, F_OK) != 0) {
        return -1;
    }
    return write_sysfs_int("max_pool_percent", percent);
}

int memopt_get_zswap_stats(zswap_stats_t* stats) {
    return get_zswap_stats(stats);
}

int memopt_dynamic_tune(void) {
    zram_stats_t zram_stats;
    zswap_stats_t zswap_stats;
    
    get_zram_stats(&zram_stats);
    get_zswap_stats(&zswap_stats);
    
    float ratio = 0.0f;
    if (zram_stats.orig_data_size > 0) {
        ratio = (float)zram_stats.orig_data_size / (float)(zram_stats.compr_data_size + 1);
    }
    
    if (ratio > 3.0f) {
        memopt_set_zswap_pool_percent(35);
        memopt_set_zram_compressor("zstd");
    } else if (ratio > 2.0f) {
        memopt_set_zswap_pool_percent(25);
        memopt_set_zram_compressor("lz4");
    } else {
        memopt_set_zswap_pool_percent(20);
        memopt_set_zram_compressor("lzo");
    }
    
    return 0;
}

int memopt_tune_for_workload(int workload_type) {
    zswap_stats_t stats;
    get_zswap_stats(&stats);
    
    switch (workload_type) {
    case MEMOPT_WORKLOAD_AI_TRAINING:
        memopt_set_zswap_pool_percent(35);
        memopt_set_zram_size("8G");
        memopt_set_zram_compressor("zstd");
        break;
        
    case MEMOPT_WORKLOAD_AI_INFERENCE:
        memopt_set_zswap_pool_percent(30);
        memopt_set_zram_size("4G");
        memopt_set_zram_compressor("lz4");
        break;
        
    case MEMOPT_WORKLOAD_GAMING:
        memopt_set_zswap_pool_percent(25);
        memopt_set_zram_size("2G");
        memopt_set_zram_compressor("lzo");
        break;
        
    case MEMOPT_WORKLOAD_BACKGROUND:
        memopt_set_zswap_pool_percent(15);
        memopt_set_zram_size("1G");
        memopt_set_zram_compressor("lzo");
        break;
        
    default:
        memopt_tune_zswap();
        break;
    }
    
    return 0;
}