/*
 * Universal RAM Optimization Layer - Kernel Module
 * Main module initialization and core functionality
 *
 * Copyright (C) 2024 Universal RAM Optimization Project
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/psi.h>
#include <linux/cgroup.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/fs.h>

#include "uroam.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Universal RAM Optimization Project");
MODULE_DESCRIPTION("Kernel module for RAM optimization in AI workloads");
MODULE_VERSION("0.1.0");

/* Module parameters */
static bool enable_pressure_monitor = true;
module_param(enable_pressure_monitor, bool, 0644);
MODULE_PARM_DESC(enable_pressure_monitor, "Enable memory pressure monitoring");

static bool enable_allocation_hooks = false;
module_param(enable_allocation_hooks, bool, 0644);
MODULE_PARM_DESC(enable_allocation_hooks, "Enable allocation interception hooks (experimental)");

static bool enable_numa_aware = true;
module_param(enable_numa_aware, bool, 0644);
MODULE_PARM_DESC(enable_numa_aware, "Enable NUMA-aware optimizations");

/* Global module state */
struct uroam_state uroam_global_state = {
	.enabled = false,
	.pressure_level = PSI_MEMORY_SOME,
	.last_pressure_time = 0,
	.pressure_count = 0,
	.allocation_count = 0,
	.reclaim_count = 0,
	.tracked_count = 0,
	.numa_node_count = 0,
	.optimization_mode = UROAM_MODE_DEFAULT,
	.numa_aware = true,
	.compression_enabled = true,
	.dedup_enabled = true,
};

/* Spinlock for thread-safe operations */
DEFINE_SPINLOCK(uroam_state_lock);

/* Proc filesystem entries */
static struct proc_dir_entry *uroam_proc_dir;
static struct proc_dir_entry *uroam_proc_status;
static struct proc_dir_entry *uroam_proc_config;
static struct proc_dir_entry *uroam_proc_stats;

/* Sysfs entries */
static struct kobject *uroam_kobj;

/**
 * uroam_print_banner - Print module banner at load time
 */
static void uroam_print_banner(void)
{
	pr_info("Universal RAM Optimization Layer v%s loaded\n", MODULE_VERSION);
	pr_info("  Pressure Monitor: %s\n", enable_pressure_monitor ? "Enabled" : "Disabled");
	pr_info("  Allocation Hooks: %s\n", enable_allocation_hooks ? "Enabled" : "Disabled");
	pr_info("  NUMA Aware: %s\n", enable_numa_aware ? "Enabled" : "Disabled");
	pr_info("  Kernel: %s\n", UTS_RELEASE);
}

/**
 * uroam_get_pressure_level_str - Convert pressure level to string
 */
static const char *uroam_get_pressure_level_str(enum psi_pressure level)
{
	switch (level) {
	case PSI_MEMORY_NONE:
		return "NONE";
	case PSI_MEMORY_SOME:
		return "SOME";
	case PSI_MEMORY_FULL:
		return "FULL";
	default:
		return "UNKNOWN";
	}
}

/**
 * uroam_proc_status_show - Show module status via procfs
 */
static int uroam_proc_status_show(struct seq_file *m, void *v)
{
	spin_lock(&uroam_state_lock);

	seq_printf(m, "Module Status: %s\n", uroam_global_state.enabled ? "Enabled" : "Disabled");
	seq_printf(m, "Pressure Level: %s\n",
			uroam_get_pressure_level_str(uroam_global_state.pressure_level));
	seq_printf(m, "Pressure Count: %llu\n", uroam_global_state.pressure_count);
	seq_printf(m, "Allocation Count: %llu\n", uroam_global_state.allocation_count);
	seq_printf(m, "Reclaim Count: %llu\n", uroam_global_state.reclaim_count);

	spin_unlock(&uroam_state_lock);

	return 0;
}

static int uroam_proc_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, uroam_proc_status_show, NULL);
}

static const struct proc_ops uroam_proc_status_ops = {
	.proc_open = uroam_proc_status_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

/**
 * uroam_proc_config_show - Show configuration via procfs
 */
static int uroam_proc_config_show(struct seq_file *m, void *v)
{
	seq_printf(m, "enable_pressure_monitor=%d\n", enable_pressure_monitor);
	seq_printf(m, "enable_allocation_hooks=%d\n", enable_allocation_hooks);
	seq_printf(m, "enable_numa_aware=%d\n", enable_numa_aware);
	return 0;
}

static int uroam_proc_config_open(struct inode *inode, struct file *file)
{
	return single_open(file, uroam_proc_config_show, NULL);
}

static const struct proc_ops uroam_proc_config_ops = {
	.proc_open = uroam_proc_config_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

/**
 * uroam_proc_stats_show - Show statistics via procfs
 */
static int uroam_proc_stats_show(struct seq_file *m, void *v)
{
	struct sysinfo si;
	si_meminfo(&si);

	seq_printf(m, "System Memory Statistics:\n");
	seq_printf(m, "  Total RAM: %lu kB\n", si.totalram);
	seq_printf(m, "  Free RAM: %lu kB\n", si.freeram);
	seq_printf(m, "  Used RAM: %lu kB\n", si.totalram - si.freeram);
	seq_printf(m, "  Buffer RAM: %lu kB\n", si.bufferram);
	seq_printf(m, "  Cached RAM: %lu kB\n", si.totalram - si.freeram - si.bufferram);

	seq_printf(m, "\nKernel Memory Statistics:\n");
	seq_printf(m, "  Page Cache: %lukB\n", global_node_page_state(NR_FILE_PAGES) * PAGE_SIZE / 1024);
	seq_printf(m, "  Anonymous: %lukB\n", global_node_page_state(NR_ANON_MAPPED) * PAGE_SIZE / 1024);
	seq_printf(m, "  Swap Cache: %lukB\n", global_node_page_state(NR_SWAPCACHE) * PAGE_SIZE / 1024);

	return 0;
}

static int uroam_proc_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, uroam_proc_stats_show, NULL);
}

static const struct proc_ops uroam_proc_stats_ops = {
	.proc_open = uroam_proc_stats_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

/**
 * Create proc filesystem entries
 */
static int uroam_create_proc_entries(void)
{
	uroam_proc_dir = proc_mkdir("uroam", NULL);
	if (!uroam_proc_dir) {
		pr_err("Failed to create /proc/uroam directory\n");
		return -ENOMEM;
	}

	uroam_proc_status = proc_create("status", 0444, uroam_proc_dir, &uroam_proc_status_ops);
	if (!uroam_proc_status) {
		pr_err("Failed to create /proc/uroam/status\n");
		goto cleanup_dir;
	}

	uroam_proc_config = proc_create("config", 0444, uroam_proc_dir, &uroam_proc_config_ops);
	if (!uroam_proc_config) {
		pr_err("Failed to create /proc/uroam/config\n");
		goto cleanup_status;
	}

	uroam_proc_stats = proc_create("stats", 0444, uroam_proc_dir, &uroam_proc_stats_ops);
	if (!uroam_proc_stats) {
		pr_err("Failed to create /proc/uroam/stats\n");
		goto cleanup_config;
	}

	return 0;

cleanup_config:
	proc_remove(uroam_proc_config);
cleanup_status:
	proc_remove(uroam_proc_status);
cleanup_dir:
	proc_remove(uroam_proc_dir);
	return -ENOMEM;
}

/**
 * Remove proc filesystem entries
 */
static void uroam_remove_proc_entries(void)
{
	if (uroam_proc_stats)
		proc_remove(uroam_proc_stats);
	if (uroam_proc_config)
		proc_remove(uroam_proc_config);
	if (uroam_proc_status)
		proc_remove(uroam_proc_status);
	if (uroam_proc_dir)
		proc_remove(uroam_proc_dir);
}

/**
 * Sysfs show function for enabled state
 */
static ssize_t uroam_enabled_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", uroam_global_state.enabled);
}

/**
 * Sysfs store function for enabled state
 */
static ssize_t uroam_enabled_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val;
	ssize_t ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret < 0)
		return ret;

	spin_lock(&uroam_state_lock);
	uroam_global_state.enabled = !!val;
	spin_unlock(&uroam_state_lock);

	pr_info("uroam: Module %s\n", val ? "enabled" : "disabled");

	return count;
}

static struct kobj_attribute uroam_enabled_attr = __ATTR(
	enabled, 0644, uroam_enabled_show, uroam_enabled_store
);

/**
 * Create sysfs entries
 */
static int uroam_create_sysfs_entries(void)
{
	int ret;

	uroam_kobj = kobject_create_and_add("uroam", kernel_kobj);
	if (!uroam_kobj) {
		pr_err("Failed to create uroam kobject\n");
		return -ENOMEM;
	}

	ret = sysfs_create_file(uroam_kobj, &uroam_enabled_attr.attr);
	if (ret < 0) {
		pr_err("Failed to create enabled sysfs file\n");
		kobject_put(uroam_kobj);
		return ret;
	}

	return 0;
}

/**
 * Remove sysfs entries
 */
static void uroam_remove_sysfs_entries(void)
{
	if (uroam_kobj) {
		sysfs_remove_file(uroam_kobj, &uroam_enabled_attr.attr);
		kobject_put(uroam_kobj);
	}
}

/**
 * uroam_notify_pressure - Notify user-space about memory pressure
 */
void uroam_notify_pressure(enum psi_pressure level)
{
	spin_lock(&uroam_state_lock);

	uroam_global_state.pressure_level = level;
	uroam_global_state.pressure_count++;
	uroam_global_state.last_pressure_time = jiffies;

	/* TODO: Send netlink notification to user-space daemon */
	uroam_netlink_send_pressure(level);

	spin_unlock(&uroam_state_lock);

	pr_debug("uroam: Pressure level changed to %s\n", uroam_get_pressure_level_str(level));
}
EXPORT_SYMBOL(uroam_notify_pressure);

/**
 * uroam_record_allocation - Record an allocation event
 */
void uroam_record_allocation(size_t size, gfp_t gfp_flags, int numa_node)
{
	spin_lock(&uroam_state_lock);
	uroam_global_state.allocation_count++;
	spin_unlock(&uroam_state_lock);

	pr_debug("uroam: Allocation: size=%zu, gfp=0x%x, node=%d\n",
			size, gfp_flags, numa_node);
}
EXPORT_SYMBOL(uroam_record_allocation);

/**
 * uroam_record_reclaim - Record a reclaim event
 */
void uroam_record_reclaim(size_t reclaimed)
{
	spin_lock(&uroam_state_lock);
	uroam_global_state.reclaim_count++;
	spin_unlock(&uroam_state_lock);

	pr_debug("uroam: Reclaimed %zu bytes\n", reclaimed);
}
EXPORT_SYMBOL(uroam_record_reclaim);

/**
 * Module initialization
 */
static int __init uroam_init(void)
{
	int ret;

	pr_info("uroam: Initializing module\n");

	/* Initialize state */
	spin_lock_init(&uroam_state_lock);
	INIT_LIST_HEAD(&uroam_global_state.allocation_list);
	uroam_global_state.enabled = true;

	/* Create proc filesystem entries */
	ret = uroam_create_proc_entries();
	if (ret < 0) {
		pr_err("uroam: Failed to create proc entries\n");
		goto out;
	}

	/* Create sysfs entries */
	ret = uroam_create_sysfs_entries();
	if (ret < 0) {
		pr_err("uroam: Failed to create sysfs entries\n");
		goto cleanup_proc;
	}

	/* Initialize netlink socket */
	ret = uroam_netlink_init();
	if (ret < 0) {
		pr_err("uroam: Failed to initialize netlink\n");
		goto cleanup_sysfs;
	}

	/* Initialize memory pressure monitoring */
	if (enable_pressure_monitor) {
		ret = uroam_pressure_init();
		if (ret < 0) {
			pr_err("uroam: Failed to initialize pressure monitoring\n");
			goto cleanup_netlink;
		}
	}

	/* Initialize allocation hooks */
	if (enable_allocation_hooks) {
		ret = uroam_hooks_init();
		if (ret < 0) {
			pr_err("uroam: Failed to initialize allocation hooks\n");
			goto cleanup_pressure;
		}
	}

	/* Initialize NUMA awareness */
	if (enable_numa_aware) {
		ret = uroam_numa_init();
		if (ret < 0) {
			pr_err("uroam: Failed to initialize NUMA awareness\n");
			goto cleanup_hooks;
		}
	}

	/* Initialize tracking */
	ret = uroam_tracking_init();
	if (ret < 0) {
		pr_err("uroam: Failed to initialize tracking\n");
		goto cleanup_numa;
	}

	uroam_print_banner();
	pr_info("uroam: Module loaded successfully\n");

	return 0;

cleanup_numa:
	if (enable_numa_aware)
		uroam_numa_exit();
cleanup_hooks:
	if (enable_allocation_hooks)
		uroam_hooks_exit();
cleanup_pressure:
	if (enable_pressure_monitor)
		uroam_pressure_exit();
cleanup_netlink:
	uroam_netlink_exit();
cleanup_sysfs:
	uroam_remove_sysfs_entries();
cleanup_proc:
	uroam_remove_proc_entries();
out:
	return ret;
}

/**
 * Module cleanup
 */
static void __exit uroam_exit(void)
{
	pr_info("uroam: Unloading module\n");

	/* Disable module */
	spin_lock(&uroam_state_lock);
	uroam_global_state.enabled = false;
	spin_unlock(&uroam_state_lock);

	/* Cleanup components in reverse order */
	uroam_tracking_exit();

	if (enable_numa_aware)
		uroam_numa_exit();

	if (enable_allocation_hooks)
		uroam_hooks_exit();

	if (enable_pressure_monitor)
		uroam_pressure_exit();

	uroam_netlink_exit();
	uroam_remove_sysfs_entries();
	uroam_remove_proc_entries();

	pr_info("uroam: Module unloaded\n");
}

module_init(uroam_init);
module_exit(uroam_exit);
