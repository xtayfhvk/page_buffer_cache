#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/mm.h>

// 记录每个进程的内存分配数据
struct alloc_info {
    pid_t pid;                        // 进程ID
    char comm[TASK_COMM_LEN];          // 进程名称
    unsigned long rss;                 // RSS 内存（Resident Set Size）
    unsigned long cache;               // 缓存内存大小
    struct list_head list;             // 链表
};

// 保存分配调用记录的链表
static LIST_HEAD(alloc_list);
static DEFINE_SPINLOCK(alloc_list_lock);  // 用于保护链表的自旋锁

static struct timer_list my_timer;  // 定时器

// 从 /proc/[pid]/statm 获取进程内存信息
static void get_proc_mem_info(struct task_struct *task, unsigned long *rss, unsigned long *cache) {
    struct mm_struct *mm;
    *rss = 0;
    *cache = 0;

    task_lock(task);  // 锁定任务
    mm = task->mm;    // 获取进程的内存描述符

    if (mm) {
        *rss = get_mm_rss(mm) << PAGE_SHIFT;   // 获取 RSS 内存大小
        *cache = (mm->cached_vm << PAGE_SHIFT); // 获取缓存内存大小
    }

    task_unlock(task);  // 解锁任务
}

// 遍历进程并统计内存分配数据
static void collect_alloc_info(void) {
    struct task_struct *task;
    struct alloc_info *entry;
    unsigned long rss, cache;
    unsigned long flags_lock;

    spin_lock_irqsave(&alloc_list_lock, flags_lock);  // 加锁保护链表

    // 遍历所有进程
    for_each_process(task) {
        if (task->flags & PF_KTHREAD)  // 忽略内核线程
            continue;

        get_proc_mem_info(task, &rss, &cache);  // 获取内存信息

        // 遍历链表，查找是否已有该进程的记录
        list_for_each_entry(entry, &alloc_list, list) {
            if (entry->pid == task->pid) {
                entry->rss = rss;
                entry->cache = cache;
                goto next_task;
            }
        }

        // 如果链表中没有该进程的记录，创建新条目
        entry = kmalloc(sizeof(*entry), GFP_KERNEL);
        if (!entry)
            goto next_task;

        entry->pid = task->pid;
        entry->rss = rss;
        entry->cache = cache;
        get_task_comm(entry->comm, task);
        INIT_LIST_HEAD(&entry->list);
        list_add_tail(&entry->list, &alloc_list);

    next_task:
        continue;
    }

    spin_unlock_irqrestore(&alloc_list_lock, flags_lock);  // 解锁
}

// 定时器回调函数，输出统计结果到内核日志
static void my_timer_callback(struct timer_list *timer) {
    struct alloc_info *entry;
    unsigned long flags_lock;

    // 首先调用 collect_alloc_info 来收集最新的内存分配信息
    collect_alloc_info();

    // 锁住链表以保护遍历
    spin_lock_irqsave(&alloc_list_lock, flags_lock);
    
    // 输出到内核日志
    printk(KERN_INFO "=== Cache/Buffer Allocation Stats ===\n");
    list_for_each_entry(entry, &alloc_list, list) {
        printk(KERN_INFO "PID: %d | Name: %s | RSS: %lu bytes | Cache: %lu bytes\n",
               entry->pid, entry->comm, entry->rss, entry->cache);
    }
    printk(KERN_INFO "=====================================\n");
    
    spin_unlock_irqrestore(&alloc_list_lock, flags_lock);  // 解锁

    // 重启定时器，5秒后再次执行
    mod_timer(&my_timer, jiffies + msecs_to_jiffies(5000));  // 每5秒运行一次
}

// 初始化内核模块
static int __init trace_alloc_init(void) {
    // 初始化定时器
    timer_setup(&my_timer, my_timer_callback, 0);
    mod_timer(&my_timer, jiffies + msecs_to_jiffies(5000));  // 每5秒触发一次

    printk(KERN_INFO "Cache/Buffer allocation tracking module loaded\n");
    return 0;
}

// 清理内核模块
static void __exit trace_alloc_exit(void) {
    struct alloc_info *entry, *tmp;
    unsigned long flags_lock;

    // 清理链表
    spin_lock_irqsave(&alloc_list_lock, flags_lock);  // 加锁保护链表
    list_for_each_entry_safe(entry, tmp, &alloc_list, list) {
        list_del(&entry->list);
        kfree(entry);
    }
    spin_unlock_irqrestore(&alloc_list_lock, flags_lock);  // 解锁

    // 删除定时器
    del_timer(&my_timer);

    printk(KERN_INFO "Cache/Buffer allocation tracking module unloaded\n");
}

module_init(trace_alloc_init);
module_exit(trace_alloc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A module to periodically track cache/buffer allocations by processes and log to kernel message.");

