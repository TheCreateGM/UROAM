/*
 * Universal RAM Optimization Layer - Memory Allocation Hooks
 *
 * Copyright (C) 2024 Universal RAM Optimization Project
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/syscalls.h>
#include <linux/kprobes.h>
#include <linux/version.h>
#include <linux/ftrace.h>
#include <linux/ring_buffer.h>
#include <linux/percpu-defs.h>

#include "uroam.h"

#define UROAM_ALLOC_HOOK_MAX_RECORDS 8192
#define UROAM_FTRACE_BUFFER_SIZE (256 * 4096)

typedef struct {
    unsigned long address;
    size_t size;
    gfp_t gfp_flags;
    int numa_node;
    pid_t pid;
    u64 timestamp;
    int type;
} uroam_alloc_record_t;

static DEFINE_PER_CPU(uroam_alloc_record_t, alloc_records);
static unsigned int record_index = 0;

static struct ring_buffer *ftrace_buffer = NULL;
static bool ftrace_enabled = false;
static bool use_ftrace = true;

static unsigned long total_mmap_bytes = 0;
static unsigned long total_brk_bytes = 0;
static unsigned long total_malloc_bytes = 0;

module_param(use_ftrace, bool, 0644);
MODULE_PARM_DESC(use_ftrace, "Use ftrace instead of kprobes when available");

/* Original function pointers for kprobes */
static asmlinkage long (*original_sys_mmap)(unsigned long addr, unsigned long len,
		unsigned long prot, unsigned long flags,
		unsigned long fd, unsigned long off);

static asmlinkage long (*original_sys_brk)(unsigned long brk);

static asmlinkage long (*original_sys_malloc)(void *ptr, size_t size);

/* Kprobe structures */
static struct kprobe kp_mmap;
static struct kprobe kp_brk;
static struct kprobe kp_malloc;

/* Ftrace function hooks */
static void (*uroam_ftrace_alloc)(const char *name, unsigned long ip,
                                   unsigned long parent_ip, unsigned long size,
                                   gfp_t flags, int numa_node);

static noinline void uroam_ftrace_hook_mmap_entry(void *data,
                                                  unsigned long address,
                                                  unsigned long len,
                                                  unsigned long prot,
                                                  unsigned long flags,
                                                  unsigned long fd,
                                                  unsigned long off)
{
    if (!uroam_global_state.enabled)
        return;

    total_mmap_bytes += len;
}

static noinline void uroam_ftrace_hook_brk_entry(void *data, unsigned long brk)
{
    if (!uroam_global_state.enabled)
        return;

    struct mm_struct *mm = current->mm;
    if (mm) {
        unsigned long old_brk = mm->brk;
        unsigned long delta = brk > old_brk ? brk - old_brk : 0;
        total_brk_bytes += delta;
    }
}

/* Hook state */
static bool mmap_hooked = false;
static bool brk_hooked = false;
static bool malloc_hooked = false;

static void uroam_record_alloc_event(int type, unsigned long size,
                                      gfp_t gfp_flags, int numa_node)
{
    uroam_alloc_record_t *rec = per_cpu_ptr(&alloc_records, smp_processor_id());

    rec->type = type;
    rec->size = size;
    rec->gfp_flags = gfp_flags;
    rec->numa_node = numa_node;
    rec->pid = current->pid;
    rec->timestamp = ktime_get_ns();

    if (ftrace_enabled && ftrace_buffer) {
        ring_buffer_record_is_online(ftrace_buffer);
    }
}

static int uroam_ftrace_init(void)
{
    if (!use_ftrace) {
        pr_info("uroam: ftrace disabled by module parameter\n");
        return -ENODEV;
    }

    ftrace_buffer = ring_buffer_create(UROAM_FTRACE_BUFFER_SIZE);
    if (!ftrace_buffer) {
        pr_warn("uroam: failed to create ftrace buffer, falling back to kprobes\n");
        return -ENOMEM;
    }

    ftrace_enabled = true;
    pr_info("uroam: ftrace allocation hooks enabled\n");

    return 0;
}

static void uroam_ftrace_exit(void)
{
    if (ftrace_buffer) {
        ring_buffer_free(ftrace_buffer);
        ftrace_buffer = NULL;
    }
    ftrace_enabled = false;
    pr_info("uroam: ftrace hooks cleaned up\n");
}

void uroam_get_hook_stats(u64 *mmap_bytes, u64 *brk_bytes, u64 *malloc_bytes)
{
    *mmap_bytes = total_mmap_bytes;
    *brk_bytes = total_brk_bytes;
    *malloc_bytes = total_malloc_bytes;
}

int uroam_enable_hook_tracing(void)
{
    int ret = 0;

    if (use_ftrace) {
        ret = uroam_ftrace_init();
        if (ret == 0) {
            return ret;
        }
    }

    return uroam_install_kprobes();
}

void uroam_disable_hook_tracing(void)
{
    if (ftrace_enabled) {
        uroam_ftrace_exit();
    }
    uroam_remove_kprobes();
}

/**
 * uroam_hook_mmap - Hook for mmap system call
 * This is called by our kprobe substitution
 */
asmlinkage long uroam_hooked_mmap(unsigned long addr, unsigned long len,
		unsigned long prot, unsigned long flags,
		unsigned long fd, unsigned long off)
{
	long ret;
	int numa_node = numa_node_id();
	
	/* Call original mmap */
	ret = original_sys_mmap(addr, len, prot, flags, fd, off);
	
	if (ret >= 0 && uroam_global_state.enabled) {
		total_mmap_bytes += len;
		uroam_record_allocation(len, GFP_KERNEL, numa_node);
		uroam_record_alloc_event(0, len, GFP_KERNEL, numa_node);
		
		pr_debug("uroam: Intercepted mmap: len=%lu, addr=%lx\n", len, ret);
	}
	
	return ret;
}

/**
 * uroam_hook_brk - Hook for brk system call
 */
asmlinkage long uroam_hooked_brk(unsigned long brk)
{
	long ret;
	unsigned long old_brk = current->mm->brk;
	
	/* Call original brk */
	ret = original_sys_brk(brk);
	
	if (ret >= 0 && uroam_global_state.enabled) {
		size_t size = brk > old_brk ? brk - old_brk : 0;
		
		if (size > 0) {
			total_brk_bytes += size;
			uroam_record_allocation(size, GFP_KERNEL, 0);
			uroam_record_alloc_event(1, size, GFP_KERNEL, 0);
		}
		
		pr_debug("uroam: Intercepted brk: old=%lx, new=%lx, size=%zu\n",
				old_brk, brk, size);
	}
	
	return ret;
}

/**
 * uroam_kprobe_handler - Generic kprobe pre-handler
 */
static int uroam_kprobe_handler(struct kprobe *p, struct pt_regs *regs)
{
	if (!uroam_global_state.enabled)
		return 0;

	/* If this is the mmap kprobe */
	if (p == &kp_mmap) {
		if (!original_sys_mmap && p->addr) {
			original_sys_mmap = (void *)p->addr;
			pr_info("uroam: Captured original sys_mmap at %px\n", original_sys_mmap);
		}

		p->addr = (kprobe_opcode_t *)uroam_hooked_mmap;
	}
	else if (p == &kp_brk) {
		if (!original_sys_brk && p->addr) {
			original_sys_brk = (void *)p->addr;
			pr_info("uroam: Captured original sys_brk at %px\n", original_sys_brk);
		}

		p->addr = (kprobe_opcode_t *)uroam_hooked_brk;
	}

	return 0;
}

/**
 * uroam_install_kprobes - Install kprobes for system call hooking
 */
static int uroam_install_kprobes(void)
{
	int ret;

	pr_info("uroam: Installing allocation hooks (kprobes)\n");

	if (use_ftrace && ftrace_enabled) {
		pr_info("uroam: Using ftrace for allocation tracking\n");
		return 0;
	}

	kp_mmap.kp.symbol_name = "sys_mmap";
	kp_mmap.pre_handler = uroam_kprobe_handler;
	kp_mmap.post_handler = NULL;
	kp_mmap.fault_handler = NULL;
	kp_mmap.break_handler = NULL;

	ret = register_kprobe(&kp_mmap);
	if (ret < 0) {
		pr_err("uroam: Failed to register mmap kprobe: %d\n", ret);
		return ret;
	}
	mmap_hooked = true;
	pr_info("uroam: Installed mmap kprobe\n");

	kp_brk.kp.symbol_name = "sys_brk";
	kp_brk.pre_handler = uroam_kprobe_handler;
	kp_brk.post_handler = NULL;
	kp_brk.fault_handler = NULL;
	kp_brk.break_handler = NULL;

	ret = register_kprobe(&kp_brk);
	if (ret < 0) {
		pr_err("uroam: Failed to register brk kprobe: %d\n", ret);
		unregister_kprobe(&kp_mmap);
		mmap_hooked = false;
		return ret;
	}
	brk_hooked = true;
	pr_info("uroam: Installed brk kprobe\n");

	return 0;
}

/**
 * uroam_remove_kprobes - Remove kprobes
 */
static void uroam_remove_kprobes(void)
{
	if (mmap_hooked) {
		unregister_kprobe(&kp_mmap);
		mmap_hooked = false;
		pr_info("uroam: Removed mmap kprobe\n");
	}
	
	if (brk_hooked) {
		unregister_kprobe(&kp_brk);
		brk_hooked = false;
		pr_info("uroam: Removed brk kprobe\n");
	}
}

/**
 * uroam_hooks_init - Initialize allocation hooks
 */
int uroam_hooks_init(void)
{
	int ret = 0;

	pr_info("uroam: Initializing allocation hooks\n");

	ret = uroam_enable_hook_tracing();
	if (ret < 0) {
		pr_warn("uroam: hook tracing failed (non-fatal), continuing without hooks\n");
		return 0;
	}

	pr_info("uroam: Allocation hooks initialized (method: %s)\n",
	        ftrace_enabled ? "ftrace" : "kprobes");

	return ret;
}

/**
 * uroam_hooks_exit - Cleanup allocation hooks
 */
void uroam_hooks_exit(void)
{
	pr_info("uroam: Cleaning up allocation hooks\n");

	uroam_disable_hook_tracing();

	pr_info("uroam: Allocation hooks cleaned up\n");
}

void uroam_get_alloc_stats(u64 *mmap_bytes, u64 *brk_bytes, u64 *total_bytes)
{
	*mmap_bytes = total_mmap_bytes;
	*brk_bytes = total_brk_bytes;
	*total_bytes = total_mmap_bytes + total_brk_bytes;
}
