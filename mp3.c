#define LINUX

#include <linux/module.h>
#include <linux/kernel.h>
#include "mp3_given.h"

#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/cdev.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tianyi Huang <tianyih5@illinois.edu>");

// #define DEBUG 1
#define IO_BUFFER 128
#define BUFFER_SIZE (128 * 4096) // 128 pages of 4 KB each
#define MAX_BUFFER (BUFFER_SIZE / sizeof(sample_data_t))

DEFINE_MUTEX(work_list_lock);

static void *buffer;                   // buffer for profiler data
static unsigned long buffer_index = 0; // Track the current position in the buffer
static int task_number = 0;

static struct proc_dir_entry *entry;
static struct proc_dir_entry *status_file;
static LIST_HEAD(work_list);

static struct delayed_work profiler_work;
static struct workqueue_struct *profiler_wq;

static dev_t dev_num;
static struct cdev *my_cdev;

unsigned long total_min_flt; // Total minor fault count
unsigned long total_maj_flt; // Total major fault count
unsigned long total_cpu_use; // Total CPU utilization

// PCB struct for work
typedef struct work_block
{
   long pid;
   // unsigned long utime;
   // unsigned long stime;
   // unsigned long maj_flt;
   // unsigned long min_flt;
   struct list_head list;
} work_block;

// Profiler data
typedef struct
{
   unsigned long jiffies; // Current jiffies value
   unsigned long min_flt; // Total minor fault count
   unsigned long maj_flt; // Total major fault count
   unsigned long cpu_use; // Total CPU utilization
} sample_data_t;

// Check work exist
bool check_work_exist(int pid)
{
   struct work_block *tmp;
   list_for_each_entry(tmp, &work_list, list)
   {
      if (tmp->pid == pid)
         return 1;
   }
   return 0;
}

// Registration handler
void reg_pid(long pid)
{
   work_block *cur_work;

   if (find_task_by_pid(pid) == NULL)
      return;

   cur_work = kzalloc(sizeof(work_block), GFP_KERNEL);

   mutex_lock(&work_list_lock);
   cur_work->pid = pid;
   if (!check_work_exist(pid))
   {
      INIT_LIST_HEAD(&cur_work->list);
      list_add_tail(&cur_work->list, &work_list);

      // scheduling the profiler work
      task_number++;

      if (task_number == 1)
         queue_delayed_work(profiler_wq, &profiler_work, msecs_to_jiffies(50));

      printk(KERN_INFO "Registration PID: %lu\n", pid);
   }
   else
      kfree(cur_work);
   mutex_unlock(&work_list_lock);
}

// Unregistration handler
void unreg_pid(long pid)
{
   work_block *tmp;
   work_block *tmp_safe;

   mutex_lock(&work_list_lock);
   list_for_each_entry_safe(tmp, tmp_safe, &work_list, list)
   {
      if (tmp->pid == pid)
      {
         list_del(&tmp->list);
         kfree(tmp);
         task_number--;
         if (task_number == 0)
            cancel_delayed_work_sync(&profiler_work);
         printk(KERN_INFO "Unregistration PID: %lu\n", pid);
         break;
      }
   }
   mutex_unlock(&work_list_lock);
}

// Worker handler
void profiler_work_handler(struct work_struct *work)
{
   // unsigned long total_min_flt = 0, total_maj_flt = 0, total_cpu_use = 0;
   unsigned long min_flt = 0, maj_flt = 0, utime = 0, stime = 0;
   struct list_head *pos, *n;
   work_block *wb;

   // Iterate through the work list and accumulate data
   mutex_lock(&work_list_lock);
   list_for_each_safe(pos, n, &work_list)
   {
      wb = list_entry(pos, work_block, list);
      if (get_cpu_use(wb->pid, &min_flt, &maj_flt, &utime, &stime) == 0)
      {

         // accumulative page fault and CPU use
         total_min_flt += min_flt;
         total_maj_flt += maj_flt;
         total_cpu_use += utime + stime;
      }
      else
      {
         // Pid not exist anymore, maybe due to interupt
         unreg_pid(wb->pid);
      }
   }
   mutex_unlock(&work_list_lock);

   // write data into the shared buffer
   sample_data_t *sample_buffer = (sample_data_t *)buffer;

   sample_buffer[buffer_index].jiffies = jiffies;
   sample_buffer[buffer_index].min_flt = total_min_flt;
   sample_buffer[buffer_index].maj_flt = total_maj_flt;
   sample_buffer[buffer_index].cpu_use = total_cpu_use;

#ifdef DEBUG
   printk(KERN_INFO "DEBUG MODE: Sample at index %lu - Jiffies: %lu, Min Flt: %lu, Maj Flt: %lu, CPU Use: %lu\n",
          buffer_index,
          sample_buffer[buffer_index].jiffies,
          sample_buffer[buffer_index].min_flt,
          sample_buffer[buffer_index].maj_flt,
          sample_buffer[buffer_index].cpu_use);

   buffer_index = (buffer_index + 1) % MAX_BUFFER;
   queue_delayed_work(profiler_wq, &profiler_work, msecs_to_jiffies(50));
   return;
#endif

   // queue like buffer
   buffer_index = (buffer_index + 1) % MAX_BUFFER;
   // Reschedule the work
   queue_delayed_work(profiler_wq, &profiler_work, msecs_to_jiffies(50));
}

static ssize_t status_write_callback(struct file *file, const char __user *buf,
                                     size_t size, loff_t *pos)
{
   char kernel_buf[IO_BUFFER];
   ssize_t bytes_read;
   long pid;
   char command;
   int ret;

   if (size > IO_BUFFER - 1 || copy_from_user(kernel_buf, buf, size) != 0)
      return -EFAULT;

   kernel_buf[size] = '\0';
   ret = sscanf(kernel_buf, "%c %ld", &command, &pid);

   if (ret != 2)
      return -EFAULT;
   if (command == 'R')
      reg_pid(pid);
   else if (command == 'U')
      unreg_pid(pid);
   else
      return -EFAULT;

   bytes_read = size;
   *pos += bytes_read;

   return bytes_read;
}

static ssize_t status_read_callback(struct file *file, char __user *buf,
                                    size_t size, loff_t *pos)
{
   work_block *start, *tmp;
   char kernel_buf[IO_BUFFER];
   int bytes_written = 0;

   if (*pos > 0)
      return 0;

   list_for_each_entry_safe(start, tmp, &work_list, list)
   {
      int buf_written = snprintf(kernel_buf + bytes_written, sizeof(kernel_buf) - bytes_written,
                                 "%ld\n", start->pid);
      bytes_written += buf_written;
   }

   // Copy the kernel buffer to user space
   if (copy_to_user(buf, kernel_buf, bytes_written))
      return -EFAULT;

   *pos += bytes_written;

   return bytes_written;
}

static int device_open(struct inode *inode, struct file *file)
{
   return 0;
}

static int device_release(struct inode *inode, struct file *file)
{
   return 0;
}

static int device_mmap(struct file *file, struct vm_area_struct *vm)
{
   unsigned long size = vm->vm_end - vm->vm_start;
   int ret = 0;
   size_t i;
   if (size > BUFFER_SIZE)
      return -EINVAL;
   for (i = 0; i < size; i += PAGE_SIZE)
   {
      unsigned long pfn = vmalloc_to_pfn(buffer + i);
      ret = remap_pfn_range(vm, vm->vm_start + i, pfn, PAGE_SIZE, vm->vm_page_prot);
   }

   return ret;
}

static struct file_operations fops = {
    .open = device_open,
    .release = device_release,
    .mmap = device_mmap,
    .owner = THIS_MODULE,
};

static const struct proc_ops lockd_end_grace_proc_ops = {
    .proc_write = status_write_callback,
    .proc_read = status_read_callback,
    .proc_lseek = default_llseek,
    .proc_release = simple_transaction_release,
};

// mp3_init - Called when module is loaded
int __init mp3_init(void)
{
   unsigned long i;
   struct page *page;
#ifdef DEBUG
   printk(KERN_ALERT "MP3 MODULE LOADING\n");
#endif
   entry = proc_mkdir("mp3", NULL);
   if (!entry)
      return -ENOMEM;
   status_file =
       proc_create("status", 0666, entry, &lockd_end_grace_proc_ops);
   if (!status_file)
      return -ENOMEM;

   // Allocate the buffer
   buffer = vzalloc(BUFFER_SIZE);
   if (!buffer)
      return -ENOMEM;

   // Set the PG_reserved bit for each page
   for (i = 0; i < BUFFER_SIZE; i += PAGE_SIZE)
   {
      page = vmalloc_to_page(buffer + i);
      if (page)
         SetPageReserved(page);
      else
         printk(KERN_ALERT "Failed to reserve page\n");
   }

   // Initialize the worker queue
   INIT_DELAYED_WORK(&profiler_work, profiler_work_handler);
   profiler_wq = create_singlethread_workqueue("profiler_wq");
   if (!profiler_wq)
   {
      pr_err("Error creating workqueue\n");
      return -ENOMEM;
   }

   // Allocate a device number
   dev_num = MKDEV(423, 0);
   register_chrdev_region(dev_num, 1, "mp3_device");
   my_cdev = cdev_alloc();
   cdev_init(my_cdev, &fops);
   cdev_add(my_cdev, dev_num, 1);

   printk(KERN_ALERT "MP3 MODULE LOADED\n");
   return 0;
}

// mp3_exit - Called when module is unloaded
void __exit mp3_exit(void)
{
   work_block *tmp;
   work_block *tmp_safe;
   unsigned long i;
   struct page *page;
#ifdef DEBUG
   printk(KERN_ALERT "MP3 MODULE UNLOADING\n");
#endif
   remove_proc_entry("status", entry);
   remove_proc_entry("mp3", NULL);

   // Iterate over the work list and free each work_block
   mutex_lock(&work_list_lock);
   list_for_each_entry_safe(tmp, tmp_safe, &work_list, list)
   {
      list_del(&tmp->list);
      kfree(tmp);
   }
   mutex_unlock(&work_list_lock);

   // Remove the character device
   cdev_del(my_cdev);
   unregister_chrdev_region(dev_num, 1);

   // Clear the workqueue
   cancel_delayed_work_sync(&profiler_work);
   destroy_workqueue(profiler_wq);

   // Clear the PG_reserved bit for each page
   for (i = 0; i < BUFFER_SIZE; i += PAGE_SIZE)
   {
      page = vmalloc_to_page(buffer + i);
      if (page)
         ClearPageReserved(page);
   }

   // Free the buffer
   vfree(buffer);

   printk(KERN_ALERT "MP3 MODULE UNLOADED\n");
}

// Register init and exit funtions
module_init(mp3_init);
module_exit(mp3_exit);
