#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/uio.h>

#define BUFFER_CACHE_SIZE 1024  // 记录的历史 Buffer Cache 数量

struct process_cache_stats {
    pid_t tgid;                 // 进程组ID (父进程 PID)
    char comm[TASK_COMM_LEN];   // 进程名
    unsigned long prev_buffer_cache;  // 上次记录的 Buffer Cache 使用量
    unsigned long current_buffer_cache;  // 当前记录的 Buffer Cache 使用量
    unsigned long growth;  // 增长量
};

// 存储每个进程的 Buffer Cache 使用情况
static struct process_cache_stats process_stats[BUFFER_CACHE_SIZE];
static int num_processes = 0;

static struct timer_list my_timer;

// 获取单个任务的 Buffer Cache 使用量
unsigned long get_task_buffer_cache_usage(struct task_struct *task) {
    unsigned long total_buffers = 0;
    struct mm_struct *mm = task->mm;

    if (!mm)
        return 0; // 如果进程没有用户空间内存，返回0

    // 遍历页表项，计算 Buffer Cache 使用量
    total_buffers += mm->total_vm * PAGE_SIZE; // 计算总虚拟内存（简化计算）
    return total_buffers;
}

// 更新进程组的 Buffer Cache 统计
void update_process_buffer_cache(struct task_struct *task) {
    unsigned long current_buffer_cache = 0;
    struct task_struct *thread;
    int found = 0;

    // 跳过系统进程
    if (task->tgid < 1000) {
        return;
    }

    // 统计该进程组的所有线程的 Buffer Cache 使用量
    for_each_thread(task, thread) {
        current_buffer_cache += get_task_buffer_cache_usage(thread);
    }
    int i;
    for (i = 0; i < num_processes; i++) {
        if (process_stats[i].tgid == task->tgid) {
            found = 1;
            process_stats[i].growth =(current_buffer_cache > process_stats[i].prev_buffer_cache)? 
				     (current_buffer_cache - process_stats[i].prev_buffer_cache):
				     (0);
            process_stats[i].prev_buffer_cache = current_buffer_cache;
            process_stats[i].current_buffer_cache = current_buffer_cache;
            break;
        }
    }

    if (!found && num_processes < BUFFER_CACHE_SIZE) {
        // 如果是新进程组，记录其 Buffer Cache 使用情况
        process_stats[num_processes].tgid = task->tgid;
        strncpy(process_stats[num_processes].comm, task->comm, TASK_COMM_LEN);
        process_stats[num_processes].prev_buffer_cache = current_buffer_cache;
        process_stats[num_processes].current_buffer_cache = current_buffer_cache;
        process_stats[num_processes].growth = 0;
        num_processes++;
    }
}

// 定时器回调函数
static void my_timer_callback(struct timer_list *timer) {
    struct task_struct *task;

    // 遍历所有进程
    for_each_process(task) {
        update_process_buffer_cache(task);
    }

    // 打印所有进程的 Buffer Cache 统计
    printk(KERN_INFO "=== Buffer Cache Stats ===\n");
    int i;
    for (i = 0; i < num_processes; i++) {
        if (process_stats[i].growth < 1) continue;
        printk(KERN_INFO "Process Group: %s (TGID: %d) | Buffer Cache: %lu bytes | Growth: %lu bytes\n",
               process_stats[i].comm, process_stats[i].tgid,
               process_stats[i].current_buffer_cache, process_stats[i].growth);
    }
    printk(KERN_INFO "=========================\n");

    // 重新启动定时器，5秒后再次执行
    mod_timer(&my_timer, jiffies + msecs_to_jiffies(5000));
}

// 初始化模块
static int __init page_buffer_stat_init(void) {
    // 设置定时器
    timer_setup(&my_timer, my_timer_callback, 0);
    mod_timer(&my_timer, jiffies + msecs_to_jiffies(5000));  // 每5秒运行一次

    printk(KERN_INFO "Page/Buffer Cache stats module loaded\n");
    return 0;
}

// 清理模块
static void __exit page_buffer_stat_exit(void) {
    del_timer(&my_timer);  // 删除定时器
    printk(KERN_INFO "Page/Buffer Cache stats module unloaded\n");
}

module_init(page_buffer_stat_init);
module_exit(page_buffer_stat_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("A module to periodically log which process groups are using Buffer Cache.");

