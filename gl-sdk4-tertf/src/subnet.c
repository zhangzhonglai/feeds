/*
 *  Copyright (C) 2019 jianhui zhao <zhaojh329@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/rtnetlink.h>
#include <linux/proc_fs.h>
#include <linux/inet.h>
#include <linux/version.h>

#include "subnet.h"

static LIST_HEAD(subnets);
static DEFINE_SPINLOCK(lock);

static struct hlist_head subnets_index[NETDEV_HASHENTRIES];

bool subnet_exist(struct net_device *dev)
{
    struct hlist_head *head;
    struct subnet *n;

    if (!dev)
        return false;

    head = &subnets_index[dev->ifindex & (NETDEV_HASHENTRIES - 1)];

    hlist_for_each_entry_rcu(n, head, hlist) {
        if (n->ifindex == dev->ifindex)
            return true;
    }

    return false;
}

void del_subnet_dev(struct net_device *dev)
{
    struct hlist_head *head = &subnets_index[dev->ifindex & (NETDEV_HASHENTRIES - 1)];
    struct subnet *n;

    spin_lock_bh(&lock);
    hlist_for_each_entry(n, head, hlist) {
        if (n->ifindex == dev->ifindex) {
            n->ifindex = -1;
            hlist_del_rcu(&n->hlist);
            pr_info("tertf: %s changed, delete it\n", dev->name);
            break;
        }
    }
    spin_unlock_bh(&lock);
}

static void add_subnet_dev_nolock(struct net_device *dev)
{
    struct hlist_head *head = &subnets_index[dev->ifindex & (NETDEV_HASHENTRIES - 1)];
    struct subnet *net, *tmp;

    list_for_each_entry(net, &subnets, list) {
        if (!strcmp(net->ifname, dev->name)) {
            hlist_for_each_entry(tmp, head, hlist) {
                if (tmp == net)
                    return;
            }
            pr_info("tertf: %s registered, add it\n", dev->name);
            net->ifindex = dev->ifindex;
            hlist_add_head_rcu(&net->hlist, head);
            break;
        }
    }
}

void add_subnet_dev(struct net_device *dev)
{
    spin_lock_bh(&lock);
    add_subnet_dev_nolock(dev);
    spin_unlock_bh(&lock);
}

static int proc_show(struct seq_file *s, void *v)
{
    struct subnet *net;

    seq_printf(s, "ifindex ifname\n");

    spin_lock_bh(&lock);
    list_for_each_entry(net, &subnets, list) {
        seq_printf(s, "%-7d %s\n", net->ifindex, net->ifname);
    }
    spin_unlock_bh(&lock);

    return 0;
}

static void add_subnet(const char *ifname)
{
    struct net_device *dev;
    struct subnet *net;

    spin_lock_bh(&lock);

    list_for_each_entry(net, &subnets, list) {
        if (!strcmp(net->ifname, ifname)) {
            pr_err("tertf: %s already exits\n", ifname);
            goto err;
        }
    }

    net = kzalloc(sizeof(struct subnet), GFP_ATOMIC);
    if (!net) {
        pr_err("tertf: no mem\n");
        goto err;
    }

    net->ifindex = -1;

    strcpy(net->ifname, ifname);
    list_add_tail(&net->list, &subnets);

    dev = dev_get_by_name(&init_net, ifname);
    if (dev) {
        add_subnet_dev_nolock(dev);
        dev_put(dev);
    }

err:
    spin_unlock_bh(&lock);
}

static void subnet_rcu_free(struct rcu_head *head)
{
    struct subnet *n = container_of(head, struct subnet, rcu);
    kfree(n);
}

static void del_subnet(const char *ifname)
{
    struct subnet *net;
    bool found = false;

    spin_lock_bh(&lock);

    list_for_each_entry(net, &subnets, list) {
        if (!strcmp(net->ifname, ifname)) {
            found = true;
            break;
        }
    }

    if (!found) {
        spin_unlock_bh(&lock);
        return;
    }

    /* delete from subnets */
    list_del(&net->list);

    /* delete from subnets_index */
    hlist_del_rcu(&net->hlist);

    call_rcu(&net->rcu, subnet_rcu_free);

    spin_unlock_bh(&lock);
}

static void subnet_index_clear(void)
{
    struct subnet *n;
    struct hlist_node *tmp;
    int i;

    for (i = 0; i < NETDEV_HASHENTRIES; i++) {
        hlist_for_each_entry_safe(n, tmp, &subnets_index[i], hlist) {
            hlist_del_rcu(&n->hlist);
        }
    }
}

static void clr_subnet(void)
{
    struct subnet *n, *tmp;

    spin_lock_bh(&lock);

    subnet_index_clear();

    list_for_each_entry_safe(n, tmp, &subnets, list) {
        list_del(&n->list);
        call_rcu(&n->rcu, subnet_rcu_free);
    }
    spin_unlock_bh(&lock);
}

static ssize_t proc_write(struct file *file, const char __user *buf, size_t size, loff_t *ppos)
{
    char data[128] = "";
    const char *ifname = "";
    char action;
    char *p;

    if (size > sizeof(data) - 1)
        return -EINVAL;

    if (copy_from_user(data, buf, size))
        return -EFAULT;

    action = data[0];

    if (action != 'a' && action != 'd' && action != 'c')
        return -EINVAL;

    if (action != 'c') {
        ifname = strchr(data, ' ');
        if (!ifname)
            return -EINVAL;

        ifname++;
        while (*ifname == ' ')
            ifname++;

        p = strchr(ifname, '\n');
        if (p)
            *p = '\0';

        if (strlen(ifname) < 1)
            return -EINVAL;
    }

    if (action == 'a')
        add_subnet(ifname);
    else if (action == 'd')
        del_subnet(ifname);
    else
        clr_subnet();

    return size;
}

static int proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, proc_show, NULL);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
const static struct file_operations gl_proc_ops = {
    .owner      = THIS_MODULE,
    .open       = proc_open,
    .read       = seq_read,
    .write      = proc_write,
    .llseek     = seq_lseek,
    .release    = single_release
};
#else
const static struct proc_ops gl_proc_ops = {
    .proc_open       = proc_open,
    .proc_read       = seq_read,
    .proc_write      = proc_write,
    .proc_lseek     = seq_lseek,
    .proc_release    = single_release
};
#endif

int subnet_init(struct proc_dir_entry *proc)
{
    proc_create("subnet", 0644, proc, &gl_proc_ops);

    return 0;
}

void subnet_free(void)
{
    clr_subnet();
}
