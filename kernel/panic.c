/*
 *  linux/kernel/panic.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This function is used through-out the kernel (includeinh mm and fs)
 * to indicate a major problem.
 */
#include <linux/kernel.h>
#include <linux/sched.h>

void sys_sync(void);	/* it's really int */

/**根据当前的进程进行处理
 * 进程为init进程
 * 进程为非init进程
 * 然后进入死循环
*/
volatile void panic(const char * s)
{
	printk("Kernel panic: %s\n\r",s);
	if (current == task[0])
	{
		printk("In swapper task - not syncing\n\r");
	}
	else
	{
		sys_sync();
	}
	for(;;);
}
