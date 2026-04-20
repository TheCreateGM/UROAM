/*
 * Universal RAM Optimization Layer - NUMA Optimization
 *
 * Copyright (C) 2024 Universal RAM Optimization Project
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/numa.h>
#include <linux/node.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/topology.h>
#include <linux/mempolicy.h>
#include <linux/migrate.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>

#include "uroam.h"

/* NUMA node count */
static int nr_numa_nodes = 1;

/* NUMA distance lookup table */
static u8 *numa_distance_table = NULL;

/* NUMA balancer state */
static bool numa_balancer_enabled = false;
static struct work_struct numa_balance_work;

/**
 * uroam_numa_update_node_info - Update NUMA node information
 */
static void uroam_numa_update_node_info(void)
{
	int node;
	
	for (node = 0; node < nr_numa_nodes; node++) {
		if (!node_online(node))
			continue;
		
		uroam_global_state.numa_nodes[node].total_memory = 
			node_present_pages(node) * PAGE_SIZE;
		uroam_global_state.numa_nodes[node].free_memory = 
			node_free_pages(node) * PAGE_SIZE;
		uroam_global_state.numa_nodes[node].used_memory = 
			(uroam_global_state.numa_nodes[node].total_memory - 
			 uroam_global_state.numa_nodes[node].free_memory);
		uroam_global_state.numa_nodes[node].nr_pages = 
			node_present_pages(node);
		uroam_global_state.numa_nodes[node].nr_free_pages = 
			node_free_pages(node);
		
		/* Copy distance information */
		if (numa_distance_table) {
			int i, j;
			for (i = 0; i < MAX_NUMA_NODES && i < nr_numa_nodes; i++) {
				for (j = 0; j < MAX_NUMA_NODES && j < nr_numa_nodes; j++) {
					uroam_global_state.numa_nodes[node].distance[i] = 
						numa_distance_table[node * MAX_NUMA_NODES + j];
				}
			}
		}
	}
	
	uroam_global_state.numa_node_count = nr_numa_nodes;
}

/**
 * uroam_numa_init_distance_table - Initialize NUMA distance table
 */
static void uroam_numa_init_distance_table(void)
{
	int i, j;
	
	if (numa_distance_table)
		return;
	
	/* Allocate distance table */
	numa_distance_table = kzalloc(MAX_NUMA_NODES * MAX_NUMA_NODES, GFP_KERNEL);
	if (!numa_distance_table) {
		pr_warn("uroam: Failed to allocate NUMA distance table\n");
		return;
	}
	
	/* Copy distance information */
	for (i = 0; i < MAX_NUMA_NODES && i < nr_numa_nodes; i++) {
		for (j = 0; j < MAX_NUMA_NODES && j < nr_numa_nodes; j++) {
			if (i < num_possible_nodes() && j < num_possible_nodes()) {
				numa_distance_table[i * MAX_NUMA_NODES + j] = 
					node_distance(i, j);
			} else {
				numa_distance_table[i * MAX_NUMA_NODES + j] = 20; /* Default remote distance */
			}
		}
	}
}

/**
 * uroam_numa_get_best_node - Get the best NUMA node for allocation
 * 
 * Returns the best NUMA node for a process based on:
 * - Current NUMA node of the process
 * - Memory availability
 * - Workload priority
 */
int uroam_numa_get_best_node(pid_t pid, size_t size)
{
	struct task_struct *task;
	struct pid *pid_struct;
	int current_node = 0;
	int best_node = 0;
	u64 min_distance = -1;
	task = get_pid_task(find_get_pid(pid), PIDTYPE_PID);
	
	if (!task)
		return 0;
	
	current_node = task_numa_node(task);
	
	/* Check all online nodes */
	for (best_node = 0; best_node < nr_numa_nodes; best_node++) {
		if (!node_online(best_node))
			continue;
		
		/* Prefer local node if it has enough memory */
		if (best_node == current_node) {
			if (uroam_global_state.numa_nodes[best_node].free_memory >= size) {
				put_task_struct(task);
				return best_node;
			}
		}
	}
	
	/* Find node with most free memory */
	for (best_node = 0; best_node < nr_numa_nodes; best_node++) {
		if (!node_online(best_node))
			continue;
		
		if (uroam_global_state.numa_nodes[best_node].free_memory >= size) {
			/* Check distance from current node */
			u64 distance = node_distance(current_node, best_node);
			
			if (min_distance == -1 || distance < min_distance) {
				min_distance = distance;
				best_node = best_node;
			}
		}
	}
	
	put_task_struct(task);
	
	return best_node;
}

/**
 * uroam_numa_migrate_pages - Migrate pages between NUMA nodes
 */
void uroam_numa_migrate_pages(pid_t pid, int from_node, int to_node)
{
	struct task_struct *task;
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	LIST_HEAD(migratePages);
	
	pr_info("uroam: Attempting to migrate pages from node %d to %d for pid %d\n",
			from_node, to_node, pid);
	
	task = get_pid_task(find_get_pid(pid), PIDTYPE_PID);
	if (!task) {
		pr_err("uroam: Task not found for pid %d\n", pid);
		return;
	}
	
	mm = get_task_mm(task);
	if (!mm) {
		pr_err("uroam: No mm_struct for pid %d\n", pid);
		put_task_struct(task);
		return;
	}
	
	/* Build list of pages to migrate */
	spin_lock(&mm->page_table_lock);
	vma = mm->mmap;
	while (vma) {
		/* TODO: Identify pages on from_node and add to migrate list */
		vma = vma->vm_next;
	}
	spin_unlock(&mm->page_table_lock);
	
	/* Migrate pages */
	if (!list_empty(&migratePages)) {
		struct migrate_target target = { .nid = to_node, .node_mask = NULL };
		
		/* TODO: Implement actual page migration */
		pr_info("uroam: Would migrate %d pages\n", 
				list_size(&migratePages));
	}
	
	mmput(mm);
	put_task_struct(task);
}

/**
 * uroam_numa_balance_work - Work function for NUMA balancing
 */
static void uroam_numa_balance_work(struct work_struct *work)
{
	int node;
	
	if (!numa_balancer_enabled)
		return;
	
	/* Check all processes for NUMA imbalance */
	for (node = 0; node < nr_numa_nodes; node++) {
		/* TODO: Implement NUMA balancing logic
		 * - Check for processes with pages on remote nodes
		 * - Migrate pages to local node if beneficial
		 * - Consider workload priority
		 */
	}
}

/**
 * uroam_numa_enable_balancer - Enable automatic NUMA balancing
 */
void uroam_numa_enable_balancer(bool enable)
{
	numa_balancer_enabled = enable;
	
	if (enable) {
		INIT_WORK(&numa_balance_work, uroam_numa_balance_work);
		schedule_work(&numa_balance_work);
		pr_info("uroam: NUMA balancer enabled\n");
	} else {
		cancel_work_sync(&numa_balance_work);
		pr_info("uroam: NUMA balancer disabled\n");
	}
}

/**
 * uroam_numa_set_policy - Set NUMA memory policy for a task
 */
void uroam_numa_set_policy(pid_t pid, int policy, int *nodes, int nr_nodes)
{
	struct task_struct *task;
	struct mempolicy *new_policy = NULL;
	
	task = get_pid_task(find_get_pid(pid), PIDTYPE_PID);
	if (!task) {
		pr_err("uroam: Task not found for pid %d\n", pid);
		return;
	}
	
	/* Create new policy based on requested type */
	switch (policy) {
	case MPOL_DEFAULT:
		new_policy = mpol_new(MPOL_DEFAULT, NULL, 0);
		break;
		
	case MPOL PREFFERRED:
		if (nodes && nr_nodes > 0) {
			new_policy = mpol_new(MPOL_PREFERRED, nodes, 1);
		}
		break;
		
	case MPOL_BIND:
		if (nodes && nr_nodes > 0) {
			new_policy = mpol_new(MPOL_BIND, nodes, nr_nodes);
		}
		break;
		
	case MPOL_INTERLEAVE:
		if (nodes && nr_nodes > 0) {
			new_policy = mpol_new(MPOL_INTERLEAVE, nodes, nr_nodes);
		}
		break;
		
	default:
		pr_warn("uroam: Unknown NUMA policy %d\n", policy);
		put_task_struct(task);
		return;
	}
	
	if (!new_policy) {
		pr_err("uroam: Failed to create NUMA policy\n");
		put_task_struct(task);
		return;
	}
	
	/* Apply new policy */
	mpol_put(task->mempolicy);
	task->mempolicy = new_policy;
	
	pr_debug("uroam: Set NUMA policy %d for pid %d\n", policy, pid);
	
	put_task_struct(task);
}

/**
 * uroam_numa_available - Check if NUMA is available on this system
 */
static bool uroam_numa_available(void)
{
	/* Check if this is a NUMA system */
	return num_possible_nodes() > 1;
}

/**
 * uroam_numa_init - Initialize NUMA support
 */
int uroam_numa_init(void)
{
	pr_info("uroam: Initializing NUMA support\n");
	
	/* Check if NUMA is enabled */
	if (!uroam_numa_available()) {
		pr_info("uroam: NUMA not available on this system\n");
		nr_numa_nodes = 1;
		return 0;
	}
	
	/* Get number of NUMA nodes */
	nr_numa_nodes = num_online_nodes();
	if (nr_numa_nodes <= 1) {
		pr_info("uroam: Single NUMA node system\n");
		nr_numa_nodes = 1;
		return 0;
	}
	
	pr_info("uroam: NUMA nodes detected: %d\n", nr_numa_nodes);
	
	/* Initialize distance table */
	uroam_numa_init_distance_table();
	
	/* Update node information */
	uroam_numa_update_node_info();
	
	/* NUMA is available */
	pr_info("uroam: NUMA support initialized with %d nodes\n", nr_numa_nodes);
	
	return 0;
}

/**
 * uroam_numa_exit - Cleanup NUMA support
 */
void uroam_numa_exit(void)
{
	pr_info("uroam: Cleaning up NUMA support\n");
	
	/* Disable balancer */
	uroam_numa_enable_balancer(false);
	
	/* Free distance table */
	if (numa_distance_table) {
		kfree(numa_distance_table);
		numa_distance_table = NULL;
	}
	
	pr_info("uroam: NUMA support cleaned up\n");
}

/**
 * uroam_numa_update - Update NUMA information
 */
void uroam_numa_update(void)
{
	if (!uroam_numa_available() || nr_numa_nodes <= 1)
		return;
	
	/* Update node information */
	uroam_numa_update_node_info();
}
