#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/slab.h>

#define UROAM_MAX_EVENTS 8192
#define UROAM_ALLOC 1
#define UROAM_FREE 2

struct alloc_event {
    __u64 timestamp;
    __u32 pid;
    __u32 tid;
    __u32 type;
    __u64 size;
    __u64 gfp_flags;
    __u32 numa_node;
    __u64 addr;
    char comm[TASK_COMM_LEN];
};

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __type(key, int);
    __type(value, struct alloc_event);
    __uint(max_entries, 64);
} uroam_events SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, __u32);
    __type(value, struct alloc_event);
    __uint(max_entries, 4096);
} alloc_tracking SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, __u32);
    __type(value, __u64);
    __uint(max_entries, 1);
} uroam_stats SEC(".maps");

static inline __u64 get_current_numa_node(void) {
    struct mm_struct *mm = current->mm;
    if (mm)
        return mm->numa_topology_margin;
    return 0;
}

SEC("kprobe/__alloc_pages")
int BPF_KPROBE(uroam_alloc_pages, gfp_t gfp_mask, unsigned int order,
              struct page *page)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    __u32 tid = bpf_get_current_pid_tgid();
    __u64 size = (1ULL << (order + 12));
    __u64 ts = bpf_ktime_get_ns();

    struct alloc_event evt = {};
    evt.timestamp = ts;
    evt.pid = pid;
    evt.tid = tid;
    evt.type = UROAM_ALLOC;
    evt.size = size;
    evt.gfp_flags = gfp_mask;
    evt.numa_node = 0;
    bpf_get_current_comm(evt.comm, sizeof(evt.comm));

    int key = 0;
    bpf_perf_event_output(ctx, &uroam_events, BPF_F_CURRENT_CPU, &evt, sizeof(evt));

    __u64 *cnt = bpf_map_lookup_elem(&uroam_stats, &key);
    if (cnt) {
        __sync_fetch_and_add(cnt, size);
    }

    return 0;
}

SEC("kprobe/__free_pages")
int BPF_KPROBE(uroam_free_pages, struct page *page, unsigned int order)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    __u32 tid = bpf_get_current_pid_tgid();
    __u64 size = (1ULL << (order + 12));
    __u64 ts = bpf_ktime_get_ns();

    struct alloc_event evt = {};
    evt.timestamp = ts;
    evt.pid = pid;
    evt.tid = tid;
    evt.type = UROAM_FREE;
    evt.size = size;
    bpf_get_current_comm(evt.comm, sizeof(evt.comm));

    bpf_perf_event_output(ctx, &uroam_events, BPF_F_CURRENT_CPU, &evt, sizeof(evt));

    return 0;
}

SEC("kprobe/kmem_cache_alloc")
int BPF_KPROBE(uroam_cache_alloc, struct kmem_cache *cachep,
               void *ptr, gfp_t gfp_mask)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    __u32 tid = bpf_get_current_pid_tgid();
    __u64 size = cachep->object_size;
    __u64 ts = bpf_ktime_get_ns();

    struct alloc_event evt = {};
    evt.timestamp = ts;
    evt.pid = pid;
    evt.tid = tid;
    evt.type = UROAM_ALLOC;
    evt.size = size;
    evt.gfp_flags = gfp_mask;
    evt.addr = (unsigned long)ptr;
    bpf_get_current_comm(evt.comm, sizeof(evt.comm));

    bpf_perf_event_output(ctx, &uroam_events, BPF_F_CURRENT_CPU, &evt, sizeof(evt));

    return 0;
}

SEC("kprobe/kmem_cache_free")
int BPF_KPROBE(uroam_cache_free, struct kmem_cache *cachep, void *ptr)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    __u32 tid = bpf_get_current_pid_tgid();
    __u64 size = cachep->object_size;
    __u64 ts = bpf_ktime_get_ns();

    struct alloc_event evt = {};
    evt.timestamp = ts;
    evt.pid = pid;
    evt.tid = tid;
    evt.type = UROAM_FREE;
    evt.size = size;
    bpf_get_current_comm(evt.comm, sizeof(evt.comm));

    bpf_perf_event_output(ctx, &uroam_events, BPF_F_CURRENT_CPU, &evt, sizeof(evt));

    return 0;
}

SEC("kprobe/sys_mmap")
int BPF_KPROBE(uroam_sys_mmap, unsigned long addr, unsigned long len,
               unsigned long prot, unsigned long flags,
               unsigned long fd, unsigned long off)
{
    __u64 ts = bpf_ktime_get_ns();
    __u32 pid = bpf_get_current_pid_tgid() >> 32;

    struct alloc_event evt = {};
    evt.timestamp = ts;
    evt.pid = pid;
    evt.type = UROAM_ALLOC;
    evt.size = len;
    bpf_get_current_comm(evt.comm, sizeof(evt.comm));

    bpf_perf_event_output(ctx, &uroam_events, BPF_F_CURRENT_CPU, &evt, sizeof(evt));

    return 0;
}

SEC("kprobe/sys_brk")
int BPF_KPROBE(uroam_sys_brk, unsigned long brk)
{
    struct mm_struct *mm = current->mm;
    if (!mm) return 0;

    unsigned long old_brk = mm->brk;
    unsigned long delta = 0;

    if (brk > old_brk) {
        delta = brk - old_brk;
    } else {
        delta = old_brk - brk;
    }

    __u64 ts = bpf_ktime_get_ns();
    __u32 pid = bpf_get_current_pid_tgid() >> 32;

    struct alloc_event evt = {};
    evt.timestamp = ts;
    evt.pid = pid;
    evt.type = UROAM_ALLOC;
    evt.size = delta;
    bpf_get_current_comm(evt.comm, sizeof(evt.comm));

    bpf_perf_event_output(ctx, &uroam_events, BPF_F_CURRENT_CPU, &evt, sizeof(evt));

    return 0;
}

char _license[] SEC("license") = "GPL";
__u32 _version SEC("version") = 20240617;