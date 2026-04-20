/*
 * Universal RAM Optimization Layer - Kernel Netlink IPC
 *
 * Copyright (C) 2024 Universal RAM Optimization Project
 * License: GPL v2
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/sched.h>

#include "uroam.h"

/* Netlink socket */
static struct sock *uroam_nl_sock = NULL;

/* Netlink socket lock */
static DEFINE_MUTEX(uroam_nl_mutex);

/* Netlink message sequence number */
static u32 uroam_nl_seq = 0;

/**
 * uroam_nl_msg_pressure - Pressure notification message
 */
struct uroam_nl_msg_pressure {
	u8 level;
	u64 timestamp;
	u64 count;
} __attribute__((packed));

/**
 * uroam_nl_msg_stats - Statistics message
 */
struct uroam_nl_msg_stats {
	u64 allocation_count;
	u64 reclaim_count;
	u64 pressure_count;
	enum psi_pressure pressure_level;
	u32 tracked_processes;
} __attribute__((packed));

/**
 * uroam_nl_msg_process - Process information message
 */
struct uroam_nl_msg_process {
	pid_t pid;
	char comm[TASK_COMM_LEN];
	u64 memory_usage;
	u64 rss;
	u8 priority;
	u8 numa_node;
	u8 is_ai_workload;
	u8 is_gaming;
} __attribute__((packed));

/**
 * uroam_nl_msg_numa - NUMA information message
 */
struct uroam_nl_msg_numa {
	u32 node_count;
	struct {
		u32 node_id;
		u64 total;
		u64 free;
	} nodes[MAX_NUMA_NODES];
} __attribute__((packed));

/**
 * uroam_netlink_send - Generic netlink message sender
 */
static int uroam_netlink_send(u16 type, void *data, size_t len, u32 portid)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	int ret;
	
	/* Allocate sk_buff */
	skb = nlmsg_new(len, GFP_KERNEL);
	if (!skb) {
		pr_err("uroam: Failed to allocate sk_buff\n");
		return -ENOMEM;
	}
	
	/* Setup netlink message header */
	nlh = nlmsg_put(skb, portid, uroam_nl_seq++, NLMSG_DONE, len, 0);
	if (!nlh) {
		pr_err("uroam: Failed to put nlmsg\n");
		nlmsg_free(skb);
		return -EMSGSIZE;
	}
	
	/* Copy data */
	if (data)
		memcpy(nlmsg_data(nlh), data, len);
	
	/* Set message type */
	nlh->nlmsg_type = type;
	
	/* Send message */
	mutex_lock(&uroam_nl_mutex);
	if (uroam_nl_sock) {
		ret = nlmsg_unicast(uroam_nl_sock, skb, portid);
	} else {
		ret = -ENOTCONN;
	}
	mutex_unlock(&uroam_nl_mutex);
	
	if (ret < 0) {
		pr_err("uroam: Failed to send netlink message: %d\n", ret);
		return ret;
	}
	
	return 0;
}

/**
 * uroam_netlink_send_pressure - Send pressure notification
 */
void uroam_netlink_send_pressure(enum psi_pressure level)
{
	struct uroam_nl_msg_pressure msg;
	
	msg.level = (u8)level;
	msg.timestamp = jiffies;
	msg.count = uroam_global_state.pressure_count;
	
	/* Send to all connected user-space processes (portid 0 = broadcast) */
	uroam_netlink_send(UROAM_NL_MSG_PRESSURE, &msg, sizeof(msg), 0);
}

/**
 * uroam_netlink_send_stats - Send statistics to user-space
 */
int uroam_netlink_send_stats(void)
{
	struct uroam_nl_msg_stats msg;
	
	spin_lock(&uroam_state_lock);
	msg.allocation_count = uroam_global_state.allocation_count;
	msg.reclaim_count = uroam_global_state.reclaim_count;
	msg.pressure_count = uroam_global_state.pressure_count;
	msg.pressure_level = uroam_global_state.pressure_level;
	msg.tracked_processes = uroam_global_state.tracked_count;
	spin_unlock(&uroam_state_lock);
	
	return uroam_netlink_send(UROAM_NL_MSG_STATS, &msg, sizeof(msg), 0);
}

/**
 * uroam_netlink_send_process_update - Send process update
 */
int uroam_netlink_send_process_update(pid_t pid, struct uroam_process_info *info)
{
	struct uroam_nl_msg_process msg;
	
	msg.pid = pid;
	if (info) {
		strncpy(msg.comm, info->comm, TASK_COMM_LEN);
		msg.memory_usage = info->memory_usage;
		msg.rss = info->rss;
		msg.priority = info->priority;
		msg.numa_node = info->numa_node;
		msg.is_ai_workload = info->is_ai_workload;
		msg.is_gaming = info->is_gaming;
	} else {
		msg.comm[0] = '\0';
		msg.memory_usage = 0;
		msg.rss = 0;
		msg.priority = UROAM_PRIORITY_P2;
		msg.numa_node = 0;
		msg.is_ai_workload = false;
		msg.is_gaming = false;
	}
	
	return uroam_netlink_send(UROAM_NL_MSG_PROCESS, &msg, sizeof(msg), 0);
}

/**
 * uroam_netlink_rcv - Netlink message receiver
 */
static void uroam_netlink_rcv(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	struct nlattr *attrs;
	int len;
	pid_t pid;
	
	if (!skb)
		return;
	
	nlh = (struct nlmsghdr *)skb->data;
	pid = NETLINK_CB(skb).portid;
	
	pr_debug("uroam: Received netlink message type=%d from pid=%d\n",
			nlh->nlmsg_type, pid);
	
	/* Process message based on type */
	switch (nlh->nlmsg_type) {
	case UROAM_NL_MSG_PRESSURE:
		pr_debug("uroam: Pressure query received\n");
		uroam_netlink_send_pressure(uroam_global_state.pressure_level);
		break;
		
	case UROAM_NL_MSG_STATS:
		pr_debug("uroam: Stats query received\n");
		uroam_netlink_send_stats();
		break;
		
	case UROAM_NL_MSG_CONFIG:
		pr_debug("uroam: Config update received\n");
		/* TODO: Parse and apply configuration */
		break;
		
	default:
		pr_debug("uroam: Unknown message type=%d\n", nlh->nlmsg_type);
		break;
	}
	
	/* Free sk_buff */
	kfree_skb(skb);
}

/**
 * uroam_netlink_init - Initialize netlink socket
 */
int uroam_netlink_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.input = uroam_netlink_rcv,
	};
	
	pr_info("uroam: Initializing netlink socket\n");
	
	/* Create netlink socket */
	uroam_nl_sock = netlink_kernel_create(&init_net, NETLINK_UROAM, &cfg);
	if (!uroam_nl_sock) {
		pr_err("uroam: Failed to create netlink socket\n");
		return -ENOMEM;
	}
	
	pr_info("uroam: Netlink socket created (family=%d)\n", NETLINK_UROAM);
	
	return 0;
}

/**
 * uroam_netlink_exit - Cleanup netlink socket
 */
void uroam_netlink_exit(void)
{
	if (uroam_nl_sock) {
		netlink_kernel_release(uroam_nl_sock);
		uroam_nl_sock = NULL;
		pr_info("uroam: Netlink socket released\n");
	}
}
