// SPDX-License-Identifier: GPL-2.0
/*
 * uroam_kernel.c - UROAM Kernel Optimization Module
 *
 * This module provides kernel-level hooks for memory optimization,
 * including MGLRU support, memory pressure monitoring, and
 * transparent hugepage management.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/page_idle.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sysctl.h>
#include <linux/cred.h>
#include <linux/memory.h>
#include <linux/mempolicy.h>
#include <linux/workqueue.h>
#include <linux/timer.h>

#define UROAM_VERSION "1.0.0"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("UROAM Team");
MODULE_DESCRIPTION("UROAM Kernel Optimization Module");
MODULE_VERSION(UROAM_VERSION);

static struct proc_dir_entry *uroam_proc_dir;
static struct ctl_table_header *uroam_sysctl_header;

static int enable_uroam = 1;
static int enable_ksm = 0;
static int enable_compact = 0;
static int scan_interval_ms = 1000;
static int compact_interval_ms = 60000;

static struct timer_list uroam_scan_timer;
static struct work_struct uroam_scan_work;
static struct workqueue_struct *uroam_wq;

extern atomic_t mglru_gen[4];

static int uroam_mglru_enabled(void)
{
#ifdef CONFIG_LRU_GEN
    return lru_gen_enabled();
#else
    return 0;
#endif
}

static void uroam_scan_work_fn(struct work_struct *work)
{
    struct scan_control sc = {
        .nr_to_reclaim = 0,
        .gfp_mask = GFP_KERNEL,
        .order = 0,
        .priority = 0,
    };

    if (!enable_uroam)
        return;

    if (enable_ksm && ksm_run != KSM_RUN_STOP) {
        ksm_run = KSM_RUN_MERGE;
        run_ksm(1);
    }

    if (enable_compact) {
        compaction_shrink_node(NULL, 0, NULL, NULL, NULL);
    }
}

static void uroam_scan_timer_fn(struct timer_list *timer)
{
    queue_work(uroam_wq, &uroam_scan_work);
    mod_timer(timer, jiffies + msecs_to_jiffies(scan_interval_ms));
}

static int uroam_show(struct seq_file *m, void *v)
{
    struct sysinfo si;
    unsigned long nr_free_pages = 0;
    unsigned long nr_active_file = 0;
    unsigned long nr_active_anon = 0;
    unsigned long nr_inactive_file = 0;
    unsigned long nr_inactive_anon = 0;
    unsigned long nr_slab_reclaimable = 0;
    unsigned long nr_slab_unreclaimable = 0;
    unsigned long nr_swapcached = 0;
    unsigned long nr_dirty = 0;
    unsigned long nr_writeback = 0;

    si_meminfo(&si);

    nr_free_pages = si.freeram;
    nr_slab_reclaimable = si.totalswap - si.freeswap;

#ifdef CONFIG_LRU_GEN
    if (uroam_mglru_enabled()) {
        seq_printf(m, "MGLRU: enabled\n");
    } else {
        seq_printf(m, "MGLRU: disabled\n");
    }
#else
    seq_printf(m, "MGLRU: not compiled in\n");
#endif

    seq_printf(m, "Version: %s\n", UROAM_VERSION);
    seq_printf(m, "Enable: %d\n", enable_uroam);
    seq_printf(m, "KSM: %d\n", enable_ksm);
    seq_printf(m, "Compact: %d\n", enable_compact);

    seq_printf(m, "Free pages: %lu\n", nr_free_pages);
    seq_printf(m, "Total swap: %lu\n", si.totalswap);
    seq_printf(m, "Free swap: %lu\n", si.freeswap);

    return 0;
}

static int uroam_open(struct inode *inode, struct file *file)
{
    return single_open(file, uroam_show, NULL);
}

static const struct proc_ops uroam_proc_ops = {
    .proc_open = uroam_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

static ctl_table uroam_sysctl_table[] = {
    {
        .procname = "enable_uroam",
        .data = &enable_uroam,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec_minmax,
        .extra1 = SYSCTL_ZERO,
        .extra2 = SYSCTL_ONE,
    },
    {
        .procname = "enable_ksm",
        .data = &enable_ksm,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec_minmax,
        .extra1 = SYSCTL_ZERO,
        .extra2 = SYSCTL_ONE,
    },
    {
        .procname = "enable_compact",
        .data = &enable_compact,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec_minmax,
        .extra1 = SYSCTL_ZERO,
        .extra2 = SYSCTL_ONE,
    },
    {
        .procname = "scan_interval_ms",
        .data = &scan_interval_ms,
        .maxlen = sizeof(int),
        .mode = 0644,
        .proc_handler = proc_dointvec_minmax,
        .extra1 = SYSCTL_INT(100),
        .extra2 = SYSCTL_INT(60000),
    },
    { }
};

static struct ctl_table uroam_root_table[] = {
    {
        .procname = "uroam",
        .mode = 0555,
        .child = uroam_sysctl_table,
    },
    { }
};

static int __init uroam_init(void)
{
    int ret;

    pr_info("UROAM kernel module v%s\n", UROAM_VERSION);

    uroam_wq = alloc_workqueue("uroam", WQ_UNBOUND | WQ_MEM_RECLAIM, 0);
    if (!uroam_wq) {
        pr_err("UROAM: failed to create workqueue\n");
        return -ENOMEM;
    }

    INIT_WORK(&uroam_scan_work, uroam_scan_work_fn);
    timer_setup(&uroam_scan_timer, uroam_scan_timer_fn, 0);

    uroam_proc_dir = proc_mkdir("uroam", NULL);
    if (!uroam_proc_dir) {
        pr_err("UROAM: failed to create proc dir\n");
        ret = -ENOMEM;
        goto err_proc;
    }

    if (!proc_create("uroam_status", 0444, uroam_proc_dir, &uroam_proc_ops)) {
        pr_err("UROAM: failed to create proc entry\n");
        ret = -ENOMEM;
        goto err_entry;
    }

    uroam_sysctl_header = register_sysctl_table(uroam_root_table);
    if (!uroam_sysctl_header) {
        pr_err("UROAM: failed to register sysctl\n");
        ret = -ENOMEM;
        goto err_sysctl;
    }

    mod_timer(&uroam_scan_timer, jiffies + msecs_to_jiffies(5000));

    pr_info("UROAM: module loaded successfully\n");
    return 0;

err_sysctl:
    remove_proc_entry("uroam_status", uroam_proc_dir);
err_entry:
    remove_proc_entry("uroam", NULL);
err_proc:
    destroy_workqueue(uroam_wq);
    return ret;
}

static void __exit uroam_exit(void)
{
    del_timer_sync(&uroam_scan_timer);
    flush_workqueue(uroam_wq);

    if (uroam_sysctl_header)
        unregister_sysctl_table(uroam_sysctl_header);

    remove_proc_entry("uroam_status", uroam_proc_dir);
    remove_proc_entry("uroam", NULL);

    destroy_workqueue(uroam_wq);

    pr_info("UROAM kernel module unloaded\n");
}

module_init(uroam_init);
module_exit(uroam_exit);