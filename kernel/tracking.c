/*
 * Universal RAM Optimization Layer - Process and Memory Tracking
 *
 * Copyright (C) 2024 Universal RAM Optimization Project
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/pid.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/kthread.h>

#include "uroam.h"

/* Process tracking tree */
static struct rb_root uroam_process_tree = RB_ROOT;
static DEFINE_SPINLOCK(uroam_process_tree_lock);

/* Tracking thread */
static struct task_struct *tracking_thread;
static bool tracking_running = false;
static wait_queue_head_t tracking_wait_queue;

/* AI workload detection patterns */
static const char *ai_workload_patterns[] = {
	"python",
	"pytorch",
	"tensorflow",
	"onnx",
	"torch",
	"tf.",
	"onnxruntime",
	"inference",
	"model",
	"neural",
	"transformer",
	"llm",
	"langchain",
	"huggingface",
	"jupyter",
	"ipython",
	NULL
};

/* Gaming workload detection patterns */
static const char *gaming_patterns[] = {
	"steam",
	"game",
	"wine",
	"proton",
	"dxvk",
	"vk",
	"opengl",
	"gl",
	"vulkan",
	"unity",
	"unreal",
	"source",
	"engine",
	"client",
	"server",
	NULL
};

/**
 * uroam_is_ai_workload - Check if process name matches AI patterns
 */
static bool uroam_is_ai_workload(const char *comm)
{
	int i;
	
	for (i = 0; ai_workload_patterns[i] != NULL; i++) {
		if (strstr(comm, ai_workload_patterns[i]))
			return true;
	}
	
	return false;
}

/**
 * uroam_is_gaming_workload - Check if process name matches gaming patterns
 */
static bool uroam_is_gaming_workload(const char *comm)
{
	int i;
	
	for (i = 0; gaming_patterns[i] != NULL; i++) {
		if (strstr(comm, gaming_patterns[i]))
			return true;
	}
	
	return false;
}

/**
 * uroam_get_process_priority - Determine priority based on process
 */
static u8 uroam_get_process_priority(struct task_struct *task, const char *comm)
{
	/* Check for AI workloads first */
	if (uroam_is_ai_workload(comm)) {
		/* Check if it's inference (higher priority) */
		if (strstr(comm, "inference") || strstr(comm, "predict") || 
			strstr(comm, "query") || strstr(comm, "api")) {
			return UROAM_PRIORITY_P0;
		}
		/* Training workloads */
		return UROAM_PRIORITY_P2;
	}
	
	/* Check for gaming workloads */
	if (uroam_is_gaming_workload(comm)) {
		return UROAM_PRIORITY_P1;
	}
	
	/* Default priority */
	return UROAM_PRIORITY_P2;
}

/**
 * uroam_get_numa_node - Get NUMA node for a task
 */
static int uroam_get_numa_node(struct task_struct *task)
{
	if (task->mempolicy && task->mempolicy->mode == MPOL_BIND) {
		/* Task has explicit NUMA policy */
		return task->mempolicy->v.preferred_node;
	}
	
	return task_numa_node(task);
}

/**
 * uroam_get_process_memory - Get memory information for a process
 */
static void uroam_get_process_memory(struct task_struct *task,
		u64 *memory_usage, u64 *rss, u64 *swap)
{
	struct mm_struct *mm;
	*memory_usage = 0;
	*rss = 0;
	*swap = 0;
	
	mm = get_task_mm(task);
	if (!mm)
		return;
	
	/* Calculate RSS */
	if (mm->rss_stat.count[MM_FILEPAGES] + mm->rss_stat.count[MM_ANONPAGES] +
		mm->rss_stat.count[MM_SWAPENTS]) {
		*rss = (mm->rss_stat.count[MM_FILEPAGES] + 
			mm->rss_stat.count[MM_ANONPAGES]) * PAGE_SIZE;
	}
	
	/* Get swap usage - this is a simplified estimate */
	*swap = get_mm_counter(mm, MM_SWAPENTS) * PAGE_SIZE;
	
	/* Total virtual memory */
	*memory_usage = *rss + *swap + get_mm_counter(mm, MM_FILEPAGES) * PAGE_SIZE;
	
	mmput(mm);
}

/**
 * uroam_process_info_cmp - Compare process info for RB tree
 */
static int uroam_process_info_cmp(pid_t pid, struct uroam_process_info *info)
{
	if (pid < info->pid)
		return -1;
	else if (pid > info->pid)
		return 1;
	return 0;
}

/**
 * uroam_process_tree_search - Search for process in RB tree
 */
static struct uroam_process_info *uroam_process_tree_search(pid_t pid)
{
	struct rb_node *node = uroam_process_tree.rb_node;
	
	while (node) {
		struct uroam_process_info *info = container_of(node, struct uroam_process_info, node);
		int cmp = uroam_process_info_cmp(pid, info);
		
		if (cmp < 0)
			node = node->rb_left;
		else if (cmp > 0)
			node = node->rb_right;
		else
			return info;
	}
	
	return NULL;
}

/**
 * uroam_process_tree_insert - Insert process info into RB tree
 */
static int uroam_process_tree_insert(struct uroam_process_info *info)
{
	struct rb_node **link = &uroam_process_tree.rb_node;
	struct rb_node *parent = NULL;
	struct uroam_process_info *tmp;
	
	while (*link) {
		parent = *link;
		tmp = container_of(parent, struct uroam_process_info, node);
		
		if (info->pid < tmp->pid)
			link = &(*link)->rb_left;
		else if (info->pid > tmp->pid)
			link = &(*link)->rb_right;
		else
			return -EEXIST; /* Already exists */
	}
	
	/* Add new node */
	rb_link_node(&info->node, parent, link);
	rb_insert_color(&info->node, &uroam_process_tree);
	
	return 0;
}

/**
 * uroam_process_tree_remove - Remove process info from RB tree
 */
static void uroam_process_tree_remove(struct uroam_process_info *info)
{
	rb_erase(&info->node, &uroam_process_tree);
}

/**
 * uroam_tracking_update_process - Update tracking info for a process
 */
static void uroam_tracking_update_process(struct task_struct *task)
{
	pid_t pid = task->pid;
	struct uroam_process_info *info;
	char comm[TASK_COMM_LEN];
	
	/* Get process command name */
	get_task_comm(comm, task);
	
	/* Check if already tracked */
	spin_lock(&uroam_process_tree_lock);
	info = uroam_process_tree_search(pid);
	
	if (info) {
		/* Update existing entry */
		uroam_get_process_memory(task, &info->memory_usage, &info->rss, &info->swap);
		info->numa_node = uroam_get_numa_node(task);
		strncpy(info->comm, comm, TASK_COMM_LEN);
	} else if (uroam_global_state.tracked_count < MAX_TRACKED_PROCESSES) {
		/* Add new entry */
		info = &uroam_global_state.tracked_processes[uroam_global_state.tracked_count];
		info->pid = pid;
		strncpy(info->comm, comm, TASK_COMM_LEN);
		uroam_get_process_memory(task, &info->memory_usage, &info->rss, &info->swap);
		info->priority = uroam_get_process_priority(task, comm);
		info->numa_node = uroam_get_numa_node(task);
		info->is_ai_workload = uroam_is_ai_workload(comm);
		info->is_gaming = uroam_is_gaming_workload(comm);
		
		/* Initialize RB tree node */
		RB_CLEAR_NODE(&info->node);
		uroam_process_tree_insert(info);
		
		uroam_global_state.tracked_count++;
		
		/* Send notification to user-space */
		uroam_netlink_send_process_update(pid, info);
	}
	
	spin_unlock(&uroam_process_tree_lock);
}

/**
 * uroam_tracking_scan - Scan all processes
 */
static void uroam_tracking_scan(void)
{
	struct task_struct *task;
	struct pid *pid;
	
	/* Scan all processes */
	for (pid = find_get_pid(1); pid != NULL; pid = find_get_pid(pid->numbers[PIDTYPE_PID].nr + 1)) {
		task = get_pid_task(pid, PIDTYPE_PID);
		if (task) {
			uroam_tracking_update_process(task);
			put_task_struct(task);
		}
		put_pid(pid);
	}
}

/**
 * tracking_thread_fn - Thread function for periodic process tracking
 */
static int tracking_thread_fn(void *data)
{
	while (tracking_running) {
		/* Update process tracking */
		uroam_tracking_scan();
		
		/* Update NUMA information */
		if (uroam_global_state.numa_aware) {
			uroam_numa_update();
		}
		
		/* Sleep for a while */
		if (wait_event_interruptible_timeout(
				tracking_wait_queue,
				!tracking_running,
				HZ * 5) == 0) {  /* 5 second interval */
			continue;
		}
		
		break;
	}
	
	return 0;
}

/**
 * uroam_tracking_get_process - Get tracked process info by PID
 */
struct uroam_process_info *uroam_tracking_get_process(pid_t pid)
{
	struct uroam_process_info *info;
	
	spin_lock(&uroam_process_tree_lock);
	info = uroam_process_tree_search(pid);
	spin_unlock(&uroam_process_tree_lock);
	
	return info;
}

/**
 * uroam_tracking_init - Initialize process tracking
 */
int uroam_tracking_init(void)
{
	int ret = 0;
	
	pr_info("uroam: Initializing process tracking\n");
	
	/* Initialize RB tree */
	uroam_global_state.tracked_count = 0;
	
	/* Initialize wait queue */
	init_waitqueue_head(&tracking_wait_queue);
	
	/* Start tracking thread */
	tracking_running = true;
	tracking_thread = kthread_run(
			tracking_thread_fn, NULL, "uroam_tracking");
	
	if (IS_ERR(tracking_thread)) {
		int err = PTR_ERR(tracking_thread);
		pr_err("uroam: Failed to create tracking thread: %d\n", err);
		tracking_running = false;
		return err;
	}
	
	/* Initial scan */
	uroam_tracking_scan();
	
	/* Initialize NUMA tracking */
	uroam_global_state.numa_aware = true;
	if (uroam_numa_init() == 0) {
		uroam_numa_update();
	}
	
	pr_info("uroam: Process tracking initialized\n");
	
	return ret;
}

/**
 * uroam_tracking_exit - Cleanup process tracking
 */
void uroam_tracking_exit(void)
{
	pr_info("uroam: Cleaning up process tracking\n");
	
	/* Stop tracking thread */
	if (tracking_running) {
		tracking_running = false;
		wake_up_all(&tracking_wait_queue);
		
		if (tracking_thread) {
			kthread_stop(tracking_thread);
			tracking_thread = NULL;
		}
	}
	
	/* Clear tracked processes */
	spin_lock(&uroam_process_tree_lock);
	uroam_global_state.tracked_count = 0;
	spin_unlock(&uroam_process_tree_lock);
	
	pr_info("uroam: Process tracking cleaned up\n");
}
