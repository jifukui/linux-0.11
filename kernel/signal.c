/*
 *  linux/kernel/signal.c
 *
 *  (C) 1991  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/segment.h>

#include <signal.h>

volatile void do_exit(int error_code);

int sys_sgetmask()
{
	return current->blocked;
}

int sys_ssetmask(int newmask)
{
	int old=current->blocked;

	current->blocked = newmask & ~(1<<(SIGKILL-1));
	return old;
}

static inline void save_old(char * from,char * to)
{
	int i;

	verify_area(to, sizeof(struct sigaction));
	for (i=0 ; i< sizeof(struct sigaction) ; i++) 
	{
		put_fs_byte(*from,to);
		from++;
		to++;
	}
}

static inline void get_new(char * from,char * to)
{
	int i;

	for (i=0 ; i< sizeof(struct sigaction) ; i++)
	{
		*(to++) = get_fs_byte(from++);
	}
}

int sys_signal(int signum, long handler, long restorer)
{
	struct sigaction tmp;

	if (signum<1 || signum>32 || signum==SIGKILL)
	{
		return -1;
	}
	tmp.sa_handler = (void (*)(int)) handler;
	tmp.sa_mask = 0;
	tmp.sa_flags = SA_ONESHOT | SA_NOMASK;
	tmp.sa_restorer = (void (*)(void)) restorer;
	handler = (long) current->sigaction[signum-1].sa_handler;
	current->sigaction[signum-1] = tmp;
	return handler;
}

int sys_sigaction(int signum, const struct sigaction * action,
	struct sigaction * oldaction)
{
	struct sigaction tmp;

	if (signum<1 || signum>32 || signum==SIGKILL)
	{
		return -1;
	}
	tmp = current->sigaction[signum-1];
	get_new((char *) action,(char *) (signum-1+current->sigaction));
	if (oldaction)
	{
		save_old((char *) &tmp,(char *) oldaction);
	}
	if (current->sigaction[signum-1].sa_flags & SA_NOMASK)
	{
		current->sigaction[signum-1].sa_mask = 0;
	}
	else
	{
		current->sigaction[signum-1].sa_mask |= (1<<(signum-1));
	}
	return 0;
}

void do_signal(long signr,
				long eax, 
				long ebx, 
				long ecx, 
				long edx,
				long fs, 
				long es, 
				long ds,
				long eip, 
				long cs, 
				long eflags,
				unsigned long * esp, 
				long ss)
{
	unsigned long sa_handler;
	long old_eip=eip;
	/**获取当前进程的信号值为传入信息值的值减一*/
	struct sigaction * sa = current->sigaction + signr - 1;
	int longs;
	unsigned long * tmp_esp;
	/**获取信号处理函数*/
	sa_handler = (unsigned long) sa->sa_handler;
	/**对信号处理函数进程处理如果值为1直接返回，
	 * 如果值不为0判断信号类型，如果信号类型为SIGCHLD直接返回如果是其他信号调用do_exit函数*/
	if (sa_handler==1)
	{
		return;
	}
	if (!sa_handler) 
	{
		if (signr==SIGCHLD)
		{
			return;
		}
		else
		{
			do_exit(1<<(signr-1));
		}
	}
	/**判断信号标记SA_ONESHOT位的值为1清除信号处理函数*/
	if (sa->sa_flags & SA_ONESHOT)
	{
		sa->sa_handler = NULL;
	}
	/**设置ip指向信号处理函数的地址*/
	*(&eip) = sa_handler;
	/**获取信号值*/
	longs = (sa->sa_flags & SA_NOMASK)?7:8;
	/**设置栈指针的位置*/
	*(&esp) -= longs;
	/**地址校验*/
	verify_area(esp,longs*4);
	tmp_esp=esp;
	/***/
	put_fs_long((long) sa->sa_restorer,tmp_esp++);
	put_fs_long(signr,tmp_esp++);
	if (!(sa->sa_flags & SA_NOMASK))
	{
		put_fs_long(current->blocked,tmp_esp++);
	}
	put_fs_long(eax,tmp_esp++);
	put_fs_long(ecx,tmp_esp++);
	put_fs_long(edx,tmp_esp++);
	put_fs_long(eflags,tmp_esp++);
	put_fs_long(old_eip,tmp_esp++);
	/**设置当前进程的屏蔽码*/
	current->blocked |= sa->sa_mask;
}
