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

#include "uroam.h"

/* Original function pointers for kprobes */
static asmlinkage long (*original_sys_mmap)(unsigned long addr, unsigned long len,
		unsigned long prot, unsigned long flags,
		unsigned long fd, unsigned long off);

static asmlinkage long (*original_sys_brk)(unsigned long brk);

/* Kprobe structures */
static struct kprobe kp_mmap;
static struct kprobe kp_brk;

/* Hook state */
static bool mmap_hooked = false;
static bool brk_hooked = false;

/**
 * uroam_hook_mmap - Hook for mmap system call
 * This is called by our kprobe substitution
 */
asmlinkage long uroam_hooked_mmap(unsigned long addr, unsigned long len,
		unsigned long prot, unsigned long flags,
		unsigned long fd, unsigned long off)
{
	long ret;
	int numa_node = 0;
	
	/* Call original mmap */
	ret = original_sys_mmap(addr, len, prot, flags, fd, off);
	
	if (ret >= 0 && uroam_global_state.enabled) {
		/* Record allocation */
		uroam_record_allocation(len, GFP_KERNEL, numa_node);
		
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
		size_t size = brk > old_brk ? brk - old_brk : old_brk - brk;
		
		/* Record allocation */
		uroam_record_allocation(size, GFP_KERNEL, 0);
		
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
	/* If this is the mmap kprobe */
	if (p == &kp_mmap) {
		/* Save the original function pointer on first call */
		if (!original_sys_mmap && p->addr) {
			original_sys_mmap = (void *)p->addr;
			pr_info("uroam: Captured original sys_mmap at %px\n", original_sys_mmap);
		}
		
		/* Temporarily replace the function */
		p->addr = (kprobe_opcode_t *)uroam_hooked_mmap;
	}
	/* If this is the brk kprobe */
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
	
	/* Setup mmap kprobe */
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
	
	/* Setup brk kprobe */
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
	
	/* Try kprobes approach first */
	ret = uroam_install_kprobes();
	if (ret < 0) {
		pr_warn("uroam: kprobes failed, allocation hooks disabled\n");
		/* Continue without hooks - they're optional */
		return 0;
	}
	
	pr_info("uroam: Allocation hooks initialized\n");
	
	return ret;
}

/**
 * uroam_hooks_exit - Cleanup allocation hooks
 */
void uroam_hooks_exit(void)
{
	pr_info("uroam: Cleaning up allocation hooks\n");
	
	uroam_remove_kprobes();
	
	pr_info("uroam: Allocation hooks cleaned up\n");
}
