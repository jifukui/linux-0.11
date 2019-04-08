/*
 *  linux/kernel/sched.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * 'sched.c' is the main kernel file. It contains scheduling primitives
 * (sleep_on, wakeup, schedule etc) as well as a number of simple system
 * call functions (type getpid(), which just extracts a field from
 * current-task
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/sys.h>
#include <linux/fdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#include <signal.h>
/**此宏的作用是获取对应信号位的值*/
#define _S(nr) (1<<((nr)-1))
/**此宏的作用是获取除SIGKILL和SIGSTOP信号之外的其他信号值*/
#define _BLOCKABLE (~(_S(SIGKILL) | _S(SIGSTOP)))

/**显示传入进程指针的状态
 * nr:信号值
 * p：进程
*/
void show_task(int nr,struct task_struct * p)
{
	int i;
	int j = 4096-sizeof(struct task_struct);
	/**输出信号值和当前进程id和进程状态*/
	printk("%d: pid=%d, state=%d, ",nr,p->pid,p->state);
	i=0;
	while (i<j && !((char *)(p+1))[i])
	{
		i++;
	}
	/**输出当前使用的内核栈和总的内核栈的空间*/
	printk("%d (of %d) chars free in kernel stack\n\r",i,j);
}


/**显示所有进程的状态*/
void show_stat(void)
{
	int i;

	for (i=0;i<NR_TASKS;i++)
	{
		if (task[i])
		{
			show_task(i,task[i]);
		}
	}
}

#define LATCH (1193180/HZ)

extern void mem_use(void);

extern int timer_interrupt(void);
extern int system_call(void);

union task_union {
	struct task_struct task;
	char stack[PAGE_SIZE];
};

static union task_union init_task = {INIT_TASK,};

long volatile jiffies=0;
long startup_time=0;
struct task_struct *current = &(init_task.task);
struct task_struct *last_task_used_math = NULL;

struct task_struct * task[NR_TASKS] = {&(init_task.task), };

long user_stack [ PAGE_SIZE>>2 ] ;

struct {
	long * a;
	short b;
	} stack_start = { & user_stack [PAGE_SIZE>>2] , 0x10 };
/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 */
/***/
void math_state_restore()
{
	/**如果最后使用协处理器的进程为当前的进程直接退出*/
	if (last_task_used_math == current)
	{
		return;
	}
	/**等待*/
	__asm__("fwait");
	/**如果协处理器使用过，保存协处理器的状态*/
	if (last_task_used_math) 
	{
		__asm__("fnsave %0"::"m" (last_task_used_math->tss.i387));
	}
	/**设置最后使用协处理器的任务号*/
	last_task_used_math=current;
	/**如果当前进程使用过协处理器，恢复协处理器的状态
	 * 否则初始化协处理器
	*/
	if (current->used_math) 
	{
		__asm__("frstor %0"::"m" (current->tss.i387));
	} 
	else 
	{
		__asm__("fninit"::);
		current->used_math=1;
	}
}

/*
 *  'schedule()' is the scheduler function. This is GOOD CODE! There
 * probably won't be any reason to change this, as it should work well
 * in all circumstances (ie gives IO-bound processes good response etc).
 * The one thing you might take a look at is the signal-handler code here.
 *
 *   NOTE!!  Task 0 is the 'idle' task, which gets called when no other
 * tasks can run. It can not be killed, and it cannot sleep. The 'state'
 * information in task[0] is never used.
 */
/**进程调度函数*/
void schedule(void)
{
	int i,next,c;
	struct task_struct ** p;

/* check alarm, wake up any interruptible tasks that have got a signal */
	/**处理第2个任务到最后个任务*/
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
	{
		if (*p) 
		{
			/**任务的计时器存在且计时器的值小于jiffies的值
			 * 设置任务的信号为警告信号，同时设置警告时间为0
			*/
			if ((*p)->alarm && (*p)->alarm < jiffies) 
			{
					(*p)->signal |= (1<<(SIGALRM-1));
					(*p)->alarm = 0;
			}
			/**任务的信号的值与上信号屏蔽值与上不可屏蔽的值且任务的状态是可中断状态的处理
			 * 设置任务的状态为可执行状态
			*/
			if (((*p)->signal & ~(_BLOCKABLE & (*p)->blocked)) &&(*p)->state==TASK_INTERRUPTIBLE)
			{
				(*p)->state=TASK_RUNNING;
			}
		}
	}

/* this is the scheduler proper: */

	while (1) 
	{
		c = -1;
		next = 0;
		i = NR_TASKS;
		p = &task[NR_TASKS];
		while (--i) 
		{
			/**对于不存在的任务跳过*/
			if (!*--p)
			{
				continue;
			}
			/**对于任务的状态为运行状态且任务的时间片大于c
			 * 设置c的值为此进程时间片的值
			 * 设置下一个为i的值
			 * 这样应该会得到时间片最大的进程
			*/
			if ((*p)->state == TASK_RUNNING && (*p)->counter > c)
			{
				c = (*p)->counter;
				next = i;
			}
		}
		/***/
		if (c)
		{
			break;
		}
		/**检测第2个任务到最后一个任务
		 * 对于存在的任务计算时间片
		 * 时间片的值为当前的时间片左移1位加上优先级的值
		*/
		for(p = &LAST_TASK ; p > &FIRST_TASK ; --p)
		{
			if (*p)
			{
				(*p)->counter = ((*p)->counter >> 1) +(*p)->priority;
			}
		}
	}
	/**调用进程*/
	switch_to(next);
}

/**系统暂停函数
 * 设置当前进程的状态为可中断状态，即等待收到一个具体的函数再进行执行
 * 并重新调用进程处理函数
*/
int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return 0;
}

/**睡眠函数*/
void sleep_on(struct task_struct **p)
{
	struct task_struct *tmp;
	/**任务处理函数不存在的处理*/
	if (!p)
	{
		return;
	}
	/**当前的任务为init任务的处理，内核输出init程序处于休眠状态*/
	if (current == &(init_task.task))
	{
		panic("task[0] trying to sleep");
	}
	tmp = *p;
	*p = current;
	/**设置当前任务的状态为不可中断状态，及不进行信号处理*/
	current->state = TASK_UNINTERRUPTIBLE;
	/**进程调度函数*/
	schedule();
	/**如果任务存在设置任务的状态为0*/
	if (tmp)
	{
		tmp->state=0;
	}
}


/***/
void interruptible_sleep_on(struct task_struct **p)
{
	struct task_struct *tmp;

	if (!p)
	{
		return;
	}
	if (current == &(init_task.task))
	{
		panic("task[0] trying to sleep");
	}
	tmp=*p;
	*p=current;
repeat:	current->state = TASK_INTERRUPTIBLE;
	schedule();
	if (*p && *p != current) 
	{
		(**p).state=0;
		goto repeat;
	}
	*p=NULL;
	if (tmp)
	{
		tmp->state=0;
	}
}
/***唤醒程序
 * 对于存在的任务设置任务的状态为0
*/
void wake_up(struct task_struct **p)
{
	if (p && *p) 
	{
		(**p).state=0;
		*p=NULL;
	}
}

/*
 * OK, here are some floppy things that shouldn't be in the kernel
 * proper. They are here because the floppy needs a timer, and this
 * was the easiest way of doing it.
 */
static struct task_struct * wait_motor[4] = {NULL,NULL,NULL,NULL};
static int  mon_timer[4]={0,0,0,0};
static int moff_timer[4]={0,0,0,0};
unsigned char current_DOR = 0x0C;
/***/
int ticks_to_floppy_on(unsigned int nr)
{
	extern unsigned char selected;
	unsigned char mask = 0x10 << nr;

	if (nr>3)
	{
		panic("floppy_on: nr>3");
	}
	moff_timer[nr]=10000;		/* 100 s = very big :-) */
	cli();				/* use floppy_off to turn it off */
	mask |= current_DOR;
	if (!selected) 
	{
		mask &= 0xFC;
		mask |= nr;
	}
	if (mask != current_DOR) 
	{
		outb(mask,FD_DOR);
		if ((mask ^ current_DOR) & 0xf0)
		{
			mon_timer[nr] = HZ/2;
		}
		else if (mon_timer[nr] < 2)
		{
			mon_timer[nr] = 2;
		}
		current_DOR = mask;
	}
	sti();
	return mon_timer[nr];
}
/***/
void floppy_on(unsigned int nr)
{
	cli();
	while (ticks_to_floppy_on(nr))
	{
		sleep_on(nr+wait_motor);
	}
	sti();
}
/***/
void floppy_off(unsigned int nr)
{
	moff_timer[nr]=3*HZ;
}
/***/
void do_floppy_timer(void)
{
	int i;
	unsigned char mask = 0x10;

	for (i=0 ; i<4 ; i++,mask <<= 1) 
	{
		if (!(mask & current_DOR))
		{
			continue;
		}
		if (mon_timer[i]) 
		{
			if (!--mon_timer[i])
			{
				wake_up(i+wait_motor);
			}
		} 
		else if (!moff_timer[i]) 
		{
			current_DOR &= ~mask;
			outb(current_DOR,FD_DOR);
		} 
		else
		{
			moff_timer[i]--;
		}
	}
}

#define TIME_REQUESTS 64

static struct timer_list {
	long jiffies;
	void (*fn)();
	struct timer_list * next;
} timer_list[TIME_REQUESTS], * next_timer = NULL;
/**添加计时器*/
void add_timer(long jiffies, void (*fn)(void))
{
	struct timer_list * p;

	if (!fn)
	{
		return;
	}
	cli();
	if (jiffies <= 0)
	{
		(fn)();
	}
	else 
	{
		for (p = timer_list ; p < timer_list + TIME_REQUESTS ; p++)
		{
			if (!p->fn)
			{
				break;
			}
		}
		if (p >= timer_list + TIME_REQUESTS)
		{
			panic("No more time requests free");
		}
		p->fn = fn;
		p->jiffies = jiffies;
		p->next = next_timer;
		next_timer = p;
		while (p->next && p->next->jiffies < p->jiffies) 
		{
			p->jiffies -= p->next->jiffies;
			fn = p->fn;
			p->fn = p->next->fn;
			p->next->fn = fn;
			jiffies = p->jiffies;
			p->jiffies = p->next->jiffies;
			p->next->jiffies = jiffies;
			p = p->next;
		}
	}
	sti();
}
/***/
void do_timer(long cpl)
{
	extern int beepcount;
	extern void sysbeepstop(void);

	if (beepcount)
	{
		if (!--beepcount)
		{
			sysbeepstop();
		}
	}

	if (cpl)
	{
		current->utime++;
	}
	else
	{
		current->stime++;
	}

	if (next_timer) 
	{
		next_timer->jiffies--;
		while (next_timer && next_timer->jiffies <= 0) 
		{
			void (*fn)(void);
			
			fn = next_timer->fn;
			next_timer->fn = NULL;
			next_timer = next_timer->next;
			(fn)();
		}
	}
	if (current_DOR & 0xf0)
	{
		do_floppy_timer();
	}
	if ((--current->counter)>0)
	{
		return;
	}
	current->counter=0;
	if (!cpl)
	{
		return;
	}
	schedule();
}

/**系统警报*/
int sys_alarm(long seconds)
{
	int old = current->alarm;

	if (old)
	{
		old = (old - jiffies) / HZ;
	}
	current->alarm = (seconds>0)?(jiffies+HZ*seconds):0;
	return (old);
}
/**获取当前进程ID*/
int sys_getpid(void)
{
	return current->pid;
}
/**获取当前进程的父进程ID*/
int sys_getppid(void)
{
	return current->father;
}
/**获取当前用户ID*/
int sys_getuid(void)
{
	return current->uid;
}
/***/
int sys_geteuid(void)
{
	return current->euid;
}
/**获取当前组ID*/
int sys_getgid(void)
{
	return current->gid;
}
/**获取当前有效用户ID*/
int sys_getegid(void)
{
	return current->egid;
}
/**设置当前进程的优先级*/
int sys_nice(long increment)
{
	if (current->priority-increment>0)
	{
		current->priority -= increment;
	}
	return 0;
}


/**调度处理函数初始化*/
void sched_init(void)
{
	int i;
	struct desc_struct * p;

	if (sizeof(struct sigaction) != 16)
	{
		panic("Struct sigaction MUST be 16 bytes");
	}
	/**设置tss描述*/
	set_tss_desc(gdt+FIRST_TSS_ENTRY,&(init_task.task.tss));
	/**设置ldt描述*/
	set_ldt_desc(gdt+FIRST_LDT_ENTRY,&(init_task.task.ldt));
	p = gdt+2+FIRST_TSS_ENTRY;
	/**初始化认为表*/
	for(i=1;i<NR_TASKS;i++) 
	{
		task[i] = NULL;
		p->a=p->b=0;
		p++;
		p->a=p->b=0;
		p++;
	}
/* Clear NT, so that we won't have troubles with that later on */
	__asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");
	ltr(0);
	lldt(0);
	outb_p(0x36,0x43);		/* binary, mode 3, LSB/MSB, ch 0 */
	outb_p(LATCH & 0xff , 0x40);	/* LSB */
	outb(LATCH >> 8 , 0x40);	/* MSB */
	set_intr_gate(0x20,&timer_interrupt);
	outb(inb_p(0x21)&~0x01,0x21);
	set_system_gate(0x80,&system_call);
}
