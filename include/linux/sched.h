#ifndef _SCHED_H
#define _SCHED_H

#define NR_TASKS 64
#define HZ 100

#define FIRST_TASK task[0]
#define LAST_TASK task[NR_TASKS-1]

#include <linux/head.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <signal.h>

#if (NR_OPEN > 32)
#error "Currently the close-on-exec-flags are in one word, max 32 files/proc"
#endif

#define TASK_RUNNING		0
#define TASK_INTERRUPTIBLE	1
#define TASK_UNINTERRUPTIBLE	2
#define TASK_ZOMBIE		3
#define TASK_STOPPED		4

#ifndef NULL
#define NULL ((void *) 0)
#endif

extern int copy_page_tables(unsigned long from, unsigned long to, long size);
extern int free_page_tables(unsigned long from, unsigned long size);

extern void sched_init(void);
extern void schedule(void);
extern void trap_init(void);
extern void panic(const char * str);
extern int tty_write(unsigned minor,char * buf,int count);

typedef int (*fn_ptr)();

struct i387_struct {
	long	cwd;
	long	swd;
	long	twd;
	long	fip;
	long	fcs;
	long	foo;
	long	fos;
	long	st_space[20];	/* 8*10 bytes for each FP-reg = 80 bytes */
};
/**定义*/
struct tss_struct {
	long	back_link;	/* 16 high bits zero */
	long	esp0;
	long	ss0;		/* 16 high bits zero */
	long	esp1;
	long	ss1;		/* 16 high bits zero */
	long	esp2;
	long	ss2;		/* 16 high bits zero */
	long	cr3;
	long	eip;
	long	eflags;
	long	eax,ecx,edx,ebx;
	long	esp;
	long	ebp;
	long	esi;
	long	edi;
	long	es;		/* 16 high bits zero */
	long	cs;		/* 16 high bits zero */
	long	ss;		/* 16 high bits zero */
	long	ds;		/* 16 high bits zero */
	long	fs;		/* 16 high bits zero */
	long	gs;		/* 16 high bits zero */
	long	ldt;		/* 16 high bits zero */
	long	trace_bitmap;	/* bits: trace 0, bitmap 16-31 */
	struct i387_struct i387;
};
/**任务结构体，也就是进程描述符
 * state：进程状态
 * counter：任务运行时间计数，时间片
 * priority：运行优先数，数值越大运行级别越高
 * signal：信号
 * sigaction：信号执行属性结构，对应信号将要执行的操作和标志信息
 * blocked：进程信号屏蔽码
 * exit_code：进程退出值
 * start_code：代码段起始地址
 * end_code：代码段长度
 * end_data:代码长度+数据长度 的字节数
 * brk：总长度
 * start_stack：堆栈起始地址
 * pid：进程id号
 * father：父进程号
 * pgrp：进程组ID
 * session：会话号
 * leader：会话首领
 * uid：用户ID
 * euid：有效用户ID
 * suid：保存的用户组ID
 * gid：组ID
 * egid：有效组ID
 * sgid：保存的用户组ID
 * alarm：报警滴答计时器值
 * utime：用户态运行时间（滴答计时）
 * stiem：系统态运行时间（滴答计时）
 * cutime：用户态运行时间
 * cstime：系统态运行时间
 * start_time：进程开始运行时间
 * used_math：是否使用协处理器
 * tty：进程使用的tty终端的子设备
 * umask：文件创建属性屏蔽位
 * pwd：当前工作目录I节点结构指针
 * root：根目录i节点结构指针
 * executable：执行文件i结构结构指针
 * close_on_exec：执行时关闭文件句柄位图
 * filp：文件结构指针表，最多32
 * ldt：局部描述符表，0空，1代码段，2数据和堆栈段
 * tss：进程的任务状态信息结构
*/
struct task_struct {
/* these are hardcoded - don't touch */
	long state;	/* -1 unrunnable, 0 runnable, >0 stopped */
	long counter;
	long priority;
	long signal;
	struct sigaction sigaction[32];
	long blocked;	/* bitmap of masked signals */
/* various fields */
	int exit_code;
	unsigned long start_code;
	unsigned long end_code;
	unsigned long end_data;
	unsigned long brk;
	unsigned long start_stack;
	long pid;
	long father;
	long pgrp;
	long session;
	long leader;
	unsigned short uid;
	unsigned short euid;
	unsigned short suid;
	unsigned short gid;
	unsigned short egid;
	unsigned short sgid;
	long alarm;
	long utime;
	long stime;
	long cutime;
	long cstime;
	long start_time;
	unsigned short used_math;
/* file system info */
	int tty;		/* -1 if no tty, so it must be signed */
	unsigned short umask;
	struct m_inode * pwd;
	struct m_inode * root;
	struct m_inode * executable;
	unsigned long close_on_exec;
	struct file * filp[NR_OPEN];
/* ldt for this task 0 - zero 1 - cs 2 - ds&ss */
	struct desc_struct ldt[3];
/* tss for this task */
	struct tss_struct tss;
};

/*
 *  INIT_TASK is used to set up the first task table, touch at
 * your own risk!. Base=0, limit=0x9ffff (=640kB)
 */
/**初始化任务表*/
#define INIT_TASK \
/* state etc */	{ 0,15,15, \
/* signals */	0,{{},},0, \
/* ec,brk... */	0,0,0,0,0,0, \
/* pid etc.. */	0,-1,0,0,0, \
/* uid etc */	0,0,0,0,0,0, \
/* alarm */	0,0,0,0,0,0, \
/* math */	0, \
/* fs info */	-1,0022,NULL,NULL,NULL,0, \
/* filp */	{NULL,}, \
	{ \
		{0,0}, \
/* ldt */	{0x9f,0xc0fa00}, \
		{0x9f,0xc0f200}, \
	}, \
/*tss*/	{0,PAGE_SIZE+(long)&init_task,0x10,0,0,0,0,(long)&pg_dir,\
	 0,0,0,0,0,0,0,0, \
	 0,0,0x17,0x17,0x17,0x17,0x17,0x17, \
	 _LDT(0),0x80000000, \
		{} \
	}, \
}

extern struct task_struct *task[NR_TASKS];
extern struct task_struct *last_task_used_math;
extern struct task_struct *current;
extern long volatile jiffies;
extern long startup_time;

/**定义当前时间当前COMS时间加上时钟脉冲除以频率的时间*/
#define CURRENT_TIME (startup_time+jiffies/HZ)

extern void add_timer(long jiffies, void (*fn)(void));
extern void sleep_on(struct task_struct ** p);
extern void interruptible_sleep_on(struct task_struct ** p);
extern void wake_up(struct task_struct ** p);

/*
 * Entry into gdt where to find first TSS. 0-nul, 1-cs, 2-ds, 3-syscall
 * 4-TSS0, 5-LDT0, 6-TSS1 etc ...
 * GDT表中的第一个表为空不进行使用
 * 第2个表用于存放代码段
 * 第3个表用于存放数据段
 * 第4个表用于存放系统调用
 * 第5个表用于存放TTS0
 * 第6个表用于存放LDT0
 */
/**定义TSS在GDT表中的位置*/
#define FIRST_TSS_ENTRY 4
/**定义第一个TSS进入点入口的位置*/
#define FIRST_LDT_ENTRY (FIRST_TSS_ENTRY+1)
/**获取TSS表的位置*/
#define _TSS(n) ((((unsigned long) n)<<4)+(FIRST_TSS_ENTRY<<3))
/**获取LDT表的位置*/
#define _LDT(n) ((((unsigned long) n)<<4)+(FIRST_LDT_ENTRY<<3))
/**将ax寄存器的值加载到任务寄存器*/
#define ltr(n) __asm__("ltr %%ax"::"a" (_TSS(n)))
/**将ax的值加载到ldt寄存器中*/
#define lldt(n) __asm__("lldt %%ax"::"a" (_LDT(n)))
/**用于获取当前任务的任务号
 * 此处的str不是存储指令是存储任务寄存器的指令，此指令的作用是将任务寄存器中的可见部分加载到ax寄存器中
 * 将i的值减去ax寄存器中的值
 * 将数值4逻辑右移ax寄存器中的低5位
 * 
*/
#define str(n) \
__asm__("str %%ax\n\t" \
	"subl %2,%%eax\n\t" \
	"shrl $4,%%eax" \
	:"=a" (n) \
	:"a" (0),"i" (FIRST_TSS_ENTRY<<3))
/*
 *	switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 * This also clears the TS-flag if the task we switched to has used
 * tha math co-processor latest.
 */
/**切换任务到*/
#define switch_to(n) {\
struct {long a,b;} __tmp; \
__asm__("cmpl %%ecx,_current\n\t" \
	"je 1f\n\t" \
	"movw %%dx,%1\n\t" \
	"xchgl %%ecx,_current\n\t" \
	"ljmp %0\n\t" \
	"cmpl %%ecx,_last_task_used_math\n\t" \
	"jne 1f\n\t" \
	"clts\n" \
	"1:" \
	::"m" (*&__tmp.a),"m" (*&__tmp.b), \
	"d" (_TSS(n)),"c" ((long) task[n])); \
}
/**定义页对齐*/
#define PAGE_ALIGN(n) (((n)+0xfff)&0xfffff000)
/**设置基地址
 * 
*/
#define _set_base(addr,base) \
__asm__("movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %%dl,%1\n\t" \
	"movb %%dh,%2" \
	::"m" (*((addr)+2)), \
	  "m" (*((addr)+4)), \
	  "m" (*((addr)+7)), \
	  "d" (base) \
	:"dx")
/**设置限制*/
#define _set_limit(addr,limit) \
__asm__("movw %%dx,%0\n\t" \
	"rorl $16,%%edx\n\t" \
	"movb %1,%%dh\n\t" \
	"andb $0xf0,%%dh\n\t" \
	"orb %%dh,%%dl\n\t" \
	"movb %%dl,%1" \
	::"m" (*(addr)), \
	  "m" (*((addr)+6)), \
	  "d" (limit) \
	:"dx")

#define set_base(ldt,base) _set_base( ((char *)&(ldt)) , base )
#define set_limit(ldt,limit) _set_limit( ((char *)&(ldt)) , (limit-1)>>12 )
/**得到基地址*/
#define _get_base(addr) ({\
unsigned long __base; \
__asm__("movb %3,%%dh\n\t" \
	"movb %2,%%dl\n\t" \
	"shll $16,%%edx\n\t" \
	"movw %1,%%dx" \
	:"=d" (__base) \
	:"m" (*((addr)+2)), \
	 "m" (*((addr)+4)), \
	 "m" (*((addr)+7))); \
__base;})

#define get_base(ldt) _get_base( ((char *)&(ldt)) )
/**得到限制*/
#define get_limit(segment) ({ \
unsigned long __limit; \
__asm__("lsll %1,%0\n\tincl %0":"=r" (__limit):"r" (segment)); \
__limit;})

#endif
