/*
 * Universal RAM Optimization Layer - Kernel Module Headers
 *
 * Copyright (C) 2024 Universal RAM Optimization Project
 * License: GPL v2
 */

#ifndef __UROAM_H
#define __UROAM_H

#include <linux/types.h>
#include <linux/psi.h>
#include <linux/gfp.h>
#include <linux/rbtree.h>

/* Pressure levels */
#define UROAM_PRESSURE_NONE   0
#define UROAM_PRESSURE_LOW    1
#define UROAM_PRESSURE_MEDIUM 2
#define UROAM_PRESSURE_HIGH   3
#define UROAM_PRESSURE_CRITICAL 4

/* Workload priority levels */
#define UROAM_PRIORITY_P0 0  /* Highest - AI Inference */
#define UROAM_PRIORITY_P1 1  /* Gaming */
#define UROAM_PRIORITY_P2 2  /* AI Training */
#define UROAM_PRIORITY_P3 3  /* Background */
#define UROAM_PRIORITY_P4 4  /* Lowest - Idle */

/* Memory optimization modes */
#define UROAM_MODE_DEFAULT    0
#define UROAM_MODE_AGGRESSIVE 1
#define UROAM_MODE_CONSERVATIVE 2
#define UROAM_MODE_GAMING     3
#define UROAM_MODE_AI        4

/* Maximum NUMA nodes supported */
#define MAX_NUMA_NODES 8

/* Maximum tracked processes */
#define MAX_TRACKED_PROCESSES 1024

/* Netlink protocol */
#define NETLINK_UROAM 31  /* Custom netlink family */

/* Netlink message types */
#define UROAM_NL_MSG_PRESSURE  0x01
#define UROAM_NL_MSG_ALLOC     0x02
#define UROAM_NL_MSG_RECLAIM   0x03
#define UROAM_NL_MSG_STATS     0x04
#define UROAM_NL_MSG_CONFIG    0x05
#define UROAM_NL_MSG_PROCESS   0x06
#define UROAM_NL_MSG_NUMA      0x07

/* Allocation tracking structure */
struct uroam_allocation {
	void *address;
	size_t size;
	gfp_t gfp_flags;
	int numa_node;
	pid_t pid;
	u64 timestamp;
	struct list_head list;
};

/* Process tracking structure with RB-tree support */
struct uroam_process_info {
	struct rb_node node;
	pid_t pid;
	char comm[TASK_COMM_LEN];
	u64 memory_usage;
	u64 rss;
	u64 swap;
	u8 priority;
	u8 numa_node;
	bool is_ai_workload;
	bool is_gaming;
};

/* NUMA node information */
struct uroam_numa_info {
	u64 total_memory;
	u64 free_memory;
	u64 used_memory;
	u32 nr_pages;
	u32 nr_free_pages;
	u32 distance[MAX_NUMA_NODES];
};

/* Global module state */
struct uroam_state {
	bool enabled;

	/* Pressure monitoring */
	enum psi_pressure pressure_level;
	unsigned long last_pressure_time;
	u64 pressure_count;

	/* Allocation tracking */
	u64 allocation_count;
	u64 reclaim_count;
	struct list_head allocation_list;

	/* Process tracking */
	struct uroam_process_info tracked_processes[MAX_TRACKED_PROCESSES];
	u32 tracked_count;

	/* NUMA information */
	struct uroam_numa_info numa_nodes[MAX_NUMA_NODES];
	u32 numa_node_count;

	/* Configuration */
	u8 optimization_mode;
	bool numa_aware;
	bool compression_enabled;
	bool dedup_enabled;
};

/* External declaration of global state */
extern struct uroam_state uroam_global_state;

/* Function declarations */

/* main.c */
void uroam_notify_pressure(enum psi_pressure level);
void uroam_record_allocation(size_t size, gfp_t gfp_flags, int numa_node);
void uroam_record_reclaim(size_t reclaimed);

/* ipc.c */
int uroam_netlink_init(void);
void uroam_netlink_exit(void);
void uroam_netlink_send_pressure(enum psi_pressure level);
int uroam_netlink_send_stats(void);
int uroam_netlink_send_process_update(pid_t pid, struct uroam_process_info *info);

/* pressure.c */
int uroam_pressure_init(void);
void uroam_pressure_exit(void);
void uroam_pressure_check(void);

/* hooks.c */
int uroam_hooks_init(void);
void uroam_hooks_exit(void);

/* tracking.c */
int uroam_tracking_init(void);
void uroam_tracking_exit(void);
void uroam_tracking_update(void);
struct uroam_process_info *uroam_tracking_get_process(pid_t pid);

/* numa.c */
int uroam_numa_init(void);
void uroam_numa_exit(void);
void uroam_numa_update(void);
int uroam_numa_get_best_node(pid_t pid, size_t size);
void uroam_numa_migrate_pages(pid_t pid, int from_node, int to_node);

#endif /* __UROAM_H */
