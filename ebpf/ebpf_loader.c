#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <linux/perf_event.h>
#include <linux/hw_breakpoint.h>

#include "memopt.h"

#ifdef HAVE_LIBBPF
#include <bpf/btf.h>
#endif

typedef struct {
    struct bpf_object *obj;
    int map_fd;
    int prog_fd;
    int perf_fd;
    bool running;
} ebpf_context_t;

static ebpf_context_t g_ebpf_ctx = {0};
static volatile bool g_ebpf_running = false;

typedef struct {
    __u64 timestamp;
    __u32 pid;
    __u32 tid;
    __u32 type;
    __u64 size;
    __u64 gfp_flags;
    __u32 numa_node;
    __u64 addr;
    char comm[16];
} alloc_event_t;

#define UROAM_ALLOC 1
#define UROAM_FREE 2

static void print_alloc_event(alloc_event_t *evt) {
    const char *type_str = (evt->type == UROAM_ALLOC) ? "ALLOC" : "FREE";
    printf("[%llu] %s: pid=%u tid=%u size=%lluKB gfp=0x%llx comm=%s\n",
           evt->timestamp / 1000000,
           type_str,
           evt->pid,
           evt->tid,
           evt->size / 1024,
           evt->gfp_flags,
           evt->comm);
}

static int perf_event_read_cb(void *ctx, int cpu, void *data, __u32 size) {
    (void)ctx;
    (void)cpu;

    if (size < sizeof(alloc_event_t)) {
        return -1;
    }

    alloc_event_t *evt = (alloc_event_t *)data;
    print_alloc_event(evt);

    return 0;
}

static int ebpf_load_object(ebpf_context_t *ctx, const char *obj_path) {
    struct bpf_object_open_opts opts = {0};
    opts.sz = sizeof(opts);

    ctx->obj = bpf_object__open(obj_path, &opts);
    if (!ctx->obj) {
        fprintf(stderr, "Failed to open BPF object: %s\n", strerror(errno));
        return -1;
    }

    if (bpf_object__load(ctx->obj) < 0) {
        fprintf(stderr, "Failed to load BPF object: %s\n", strerror(errno));
        bpf_object__close(ctx->obj);
        ctx->obj = NULL;
        return -1;
    }

    struct bpf_program *prog = NULL;
    bpf_object__for_each_program(prog, ctx->obj) {
        ctx->prog_fd = bpf_program__fd(prog);
        if (ctx->prog_fd > 0) {
            break;
        }
    }

    ctx->map_fd = bpf_map__fd(bpf_object__find_map_by_name(ctx->obj, "uroam_events"));
    if (ctx->map_fd < 0) {
        ctx->map_fd = bpf_map__fd(bpf_object__find_map_by_name(ctx->obj, "uroam_stats"));
    }

    return 0;
}

int ebpf_init(const char *obj_path) {
    memset(&g_ebpf_ctx, 0, sizeof(g_ebpf_ctx));

    if (obj_path == NULL) {
        obj_path = "./uroam.bpf.o";
    }

    if (ebpf_load_object(&g_ebpf_ctx, obj_path) != 0) {
        fprintf(stderr, "Failed to load eBPF object\n");
        return -1;
    }

    printf("eBPF object loaded successfully\n");
    return 0;
}

int ebpf_attach(void) {
    if (!g_ebpf_ctx.obj) {
        return -1;
    }

    struct bpf_program *prog = NULL;
    bpf_object__for_each_program(prog, g_ebpf_ctx.obj) {
        int ret = bpf_program__attach(prog);
        if (ret < 0) {
            fprintf(stderr, "Failed to attach program %s: %s\n",
                    bpf_program__name(prog), strerror(errno));
            continue;
        }
    }

    g_ebpf_running = true;
    printf("eBPF programs attached\n");
    return 0;
}

int ebpf_start_monitoring(void) {
    if (!g_ebpf_ctx.obj) {
        return -1;
    }

    g_ebpf_running = true;

    if (g_ebpf_ctx.map_fd > 0) {
        int perf_fd = bpf_map__fd(bpf_object__find_map_by_name(g_ebpf_ctx.obj, "uroam_events"));
        if (perf_fd > 0) {
            printf("Started eBPF monitoring (fd=%d)\n", perf_fd);
        }
    }

    return 0;
}

void ebpf_stop_monitoring(void) {
    g_ebpf_running = false;
}

void ebpf_shutdown(void) {
    ebpf_stop_monitoring();

    if (g_ebpf_ctx.obj) {
        bpf_object__close(g_ebpf_ctx.obj);
    }

    memset(&g_ebpf_ctx, 0, sizeof(g_ebpf_ctx));
    printf("eBPF shutdown complete\n");
}

int ebpf_get_stats(__u64 *total_alloc, __u64 *total_free) {
    if (!g_ebpf_ctx.obj) {
        return -1;
    }

    int stats_fd = bpf_map__fd(bpf_object__find_map_by_name(g_ebpf_ctx.obj, "uroam_stats"));
    if (stats_fd < 0) {
        return -1;
    }

    __u32 key = 0;
    __u64 value = 0;
    int err = bpf_map_lookup_elem(stats_fd, &key, &value);

    if (err == 0 && total_alloc) {
        *total_alloc = value;
    }

    return err;
}

int ebpf_set_filter_pid(int pid) {
    if (!g_ebpf_ctx.obj) {
        return -1;
    }

    return 0;
}

typedef struct {
    char obj_path[256];
    bool verbose;
    bool polling;
    int sample_rate;
} ebpf_config_t;

static ebpf_config_t g_config = {
    .obj_path = "./uroam.bpf.o",
    .verbose = false,
    .polling = false,
    .sample_rate = 1
};

int ebpf_set_config(const char *key, const char *value) {
    if (strcmp(key, "obj_path") == 0) {
        strncpy(g_config.obj_path, value, sizeof(g_config.obj_path) - 1);
    } else if (strcmp(key, "verbose") == 0) {
        g_config.verbose = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
    } else if (strcmp(key, "polling") == 0) {
        g_config.polling = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
    } else if (strcmp(key, "sample_rate") == 0) {
        g_config.sample_rate = atoi(value);
    }

    return 0;
}

void ebpf_get_config(ebpf_config_t *config) {
    if (config) {
        memcpy(config, &g_config, sizeof(g_config));
    }
}