/*
 *  linux/kernel/exit.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <asm/segment.h>

int sys_pause(void);
int sys_close(int fd);


/**根据提供的任务结构体的指针
 * 判断此任务任务数组中是否存在
 * 如果存在设置其指针为空指针
 * 释放页，调用进度程序
*/
void release(struct task_struct * p)
{
	int i;

	if (!p)
	{
		return;
	}
	for (i=1 ; i<NR_TASKS ; i++)
	{
		if (task[i]==p) 
		{
			task[i]=NULL;
			free_page((long)p);
			schedule();
			return;
		}
	}
	panic("trying to release non-existent task");
}

/**发送信号给对应的任务（进程）发送信号
 * sig：为信号值
 * p：为进程指针
 * priv：为是否强制
*/
static inline int send_sig(long sig,struct task_struct * p,int priv)
{
	/**判断任务是否存在，且信号的范围是否正确
	 * 对于不正确的进行处理
	*/
	if (!p || sig<1 || sig>32)
	{
		return -EINVAL;
	}
	/**对于priv的值为非0值
	 * 或者当前任务的
	 * 或者当前用户为超级用户向此任务发送信号
	 */
	if (priv || (current->euid==p->euid) || suser())
	{
		p->signal |= (1<<(sig-1));
	}
	else
	{
		return -EPERM;
	}
	return 0;
}

/**杀任务的会话值为当前会话的进程*/
static void kill_session(void)
{
	/**将指针指向任务数组的末端*/
	struct task_struct **p = NR_TASKS + task;
	/**循环所有的任务处理任务0,
	 * 向任务的会话等于当前的会话的任务的信号值设置为挂断
	*/
	while (--p > &FIRST_TASK) 
	{
		if (*p && (*p)->session == current->session)
		{
			(*p)->signal |= 1<<(SIGHUP-1);
		}
	}
}

/*
 * XXX need to check permissions needed to send signals to process
 * groups, etc. etc.  kill() permissions semantics are tricky!
 */
/***/
int sys_kill(int pid,int sig)
{
	struct task_struct **p = NR_TASKS + task;
	int err, retval = 0;
	/**对于进程ID为0的处理
	 * 对于进程的组ID为当前进程的ID强制发送信号
	*/
	if (!pid)
	{
		while (--p > &FIRST_TASK) 
		{
			if (*p && (*p)->pgrp == current->pid) 
			{
				if (err=send_sig(sig,*p,1))
				{
					retval = err;
				}
			}
		} 
	}
	/**对于进程ID大于0的处理
	 * 对于进程ID为传入的pid值发送信号
	*/
	else if (pid>0) 
	{
		while (--p > &FIRST_TASK) 
		{
			if (*p && (*p)->pid == pid) 
			{
				if (err=send_sig(sig,*p,0))
				{
					retval = err;
				}
			}
		} 
	}
	/**对于进程ID等于-1的处理
	 * 对所有非0进程发送信号
	*/
	else if (pid == -1) 
	{
		while (--p > &FIRST_TASK)
		{	
			if (err = send_sig(sig,*p,0))
			{
				retval = err;
			}
		}
	}
	/**对于进程ID为小于-1的处理
	 * 对于组ID为pid的绝对值发送信号
	*/
	else
	{
		while (--p > &FIRST_TASK)
		{
			if (*p && (*p)->pgrp == -pid)
			{
				if (err = send_sig(sig,*p,0))
				{
					retval = err;
				}
			}
		}
	}
	return retval;
}


/**根据传入的pid的值
 * 在任务数组中找出与其值一致的pid
 * 并设置其信号值
*/
static void tell_father(int pid)
{
	int i;

	if (pid)
	{
		for (i=0;i<NR_TASKS;i++) 
		{
			if (!task[i])
			{
				continue;
			}
			if (task[i]->pid != pid)
			{
				continue;
			}
			task[i]->signal |= (1<<(SIGCHLD-1));
			return;
		}
	}
/* if we don't find any fathers, we just release ourselves */
/* This is not really OK. Must change it to make father 1 */
	printk("BAD BAD - no father found\n\r");
	release(current);
}

/**释放当前进程在代码段和数据段占用的内存页*/
int do_exit(long code)
{
	int i;
	/**释放当前进程在代码段和数据段占用的内存页*/
	free_page_tables(get_base(current->ldt[1]),get_limit(0x0f));
	free_page_tables(get_base(current->ldt[2]),get_limit(0x17));
	/**对于任务存在且任务的父进程为当前进程设置此任务的父进程的值为1及init进程
	 * 对于任务的状态为僵死状态向init进程发送子进程信号
	*/
	for (i=0 ; i<NR_TASKS ; i++)
	{
		if (task[i] && task[i]->father == current->pid) 
		{
			task[i]->father = 1;
			if (task[i]->state == TASK_ZOMBIE)
			{
				/* assumption task[1] is always init */
				(void) send_sig(SIGCHLD, task[1], 1);
			}
		}
	}
	/**关闭当前进程打开的文件*/
	for (i=0 ; i<NR_OPEN ; i++)
	{
		if (current->filp[i])
		{
			sys_close(i);
		}
	}
	/**对于当前的工作目录，根目录，执行程序的文件进程磁盘写回并释放*/
	iput(current->pwd);
	current->pwd=NULL;
	iput(current->root);
	current->root=NULL;
	iput(current->executable);
	current->executable=NULL;
	/**对于进程时领导进程且使用tty释放此tty*/
	if (current->leader && current->tty >= 0)
	{
		tty_table[current->tty].pgrp = 0;
	}
	/**对于使用了协处理器设置上次使用协处理器为空*/
	if (last_task_used_math == current)
	{
		last_task_used_math = NULL;
	}
	/**对于此进程为领导进程关闭会话*/
	if (current->leader)
	{
		kill_session();
	}
	/**设置当前进程的进程状态为僵死状态*/
	current->state = TASK_ZOMBIE;
	/**设置退出码*/
	current->exit_code = code;
	/**告知父进程*/
	tell_father(current->father);
	/**调用进程管理*/
	schedule();
	return (-1);	/* just to suppress warnings */
}

/**系统退出*/
int sys_exit(int error_code)
{
	return do_exit((error_code&0xff)<<8);
}


/**系统等待进程
 * pid:进程id
 * stat_addr：
 * options：
*/
int sys_waitpid(pid_t pid,unsigned long * stat_addr, int options)
{
	int flag, code;
	struct task_struct ** p;

	verify_area(stat_addr,4);
repeat:
	flag=0;
	/**从最后一个进程到第2个进程*/
	for(p = &LAST_TASK ; p > &FIRST_TASK ; --p) 
	{
		/**对于进程为空或者进程为当前进程的跳过*/
		if (!*p || *p == current)
		{
			continue;
		}
		/**对于进程的父进程ID不为当前进程的跳过*/
		if ((*p)->father != current->pid)
		{
			continue;
		}
		/**对于进程ID大于0
		 * 如果进程的进程ID不等于传入的进程ID跳过
		*/
		if (pid>0) 
		{
			if ((*p)->pid != pid)
			{
				continue;
			}
		} 
		/**对于进程ID为0且进程的组ID不为当前进程的组ID跳过*/
		else if (!pid) 
		{
			if ((*p)->pgrp != current->pgrp)
			{
				continue;
			}
		}
		/**对于传入的进程id不为-1且进程组ID不为传入的进程ID的绝对值跳过*/ 
		else if (pid != -1) 
		{
			if ((*p)->pgrp != -pid)
			{
				continue;
			}
		}
		/**根据进程状态进程处理*/
		switch ((*p)->state) 
		{
			/**对于进程的状态为停止状态*/
			case TASK_STOPPED:
				/**对于无需立即返回的处理*/
				if (!(options & WUNTRACED))
				{
					continue;
				}
				put_fs_long(0x7f,stat_addr);
				return (*p)->pid;
			/**对于进程状态为僵死状态*/
			case TASK_ZOMBIE:
				current->cutime += (*p)->utime;
				current->cstime += (*p)->stime;
				flag = (*p)->pid;
				code = (*p)->exit_code;
				release(*p);
				put_fs_long(code,stat_addr);
				return flag;
			/**其他状态的处理*/
			default:
				flag=1;
				continue;
		}
	}
	if (flag) 
	{
		if (options & WNOHANG)
		{
			return 0;
		}
		current->state=TASK_INTERRUPTIBLE;
		schedule();
		if (!(current->signal &= ~(1<<(SIGCHLD-1))))
		{
			goto repeat;
		}
		else
		{
			return -EINTR;
		}
	}
	return -ECHILD;
}


