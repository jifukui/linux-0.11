/*
 *  linux/kernel/system_call.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  system_call.s  contains the system-call low-level handling routines.
 * This also contains the timer-interrupt handler, as some of the code is
 * the same. The hd- and flopppy-interrupts are also here.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call. Ordinary interrupts
 * don't handle signal-recognition, as that would clutter them up totally
 * unnecessarily.
 *
 * Stack layout in 'ret_from_system_call':
 *
 *	 0(%esp) - %eax
 *	 4(%esp) - %ebx
 *	 8(%esp) - %ecx
 *	 C(%esp) - %edx
 *	10(%esp) - %fs
 *	14(%esp) - %es
 *	18(%esp) - %ds
 *	1C(%esp) - %eip
 *	20(%esp) - %cs
 *	24(%esp) - %eflags
 *	28(%esp) - %oldesp
 *	2C(%esp) - %oldss
 */
#定义子进程结束值17
SIG_CHLD	= 17
#定义堆栈中各个寄存器的位置
EAX		= 0x00
EBX		= 0x04
ECX		= 0x08
EDX		= 0x0C
FS		= 0x10
ES		= 0x14
DS		= 0x18
EIP		= 0x1C
CS		= 0x20
EFLAGS		= 0x24
OLDESP		= 0x28
OLDSS		= 0x2C
#任务结构各个参数的偏移位置
state	= 0		# these are offsets into the task-struct.
counter	= 4
priority = 8
signal	= 12
sigaction = 16		# MUST be 16 (=len of sigaction)
blocked = (33*16)
#信号结构中各个参数的偏移位置
# offsets within sigaction
sa_handler = 0
sa_mask = 4
sa_flags = 8
sa_restorer = 12
#内核的系统调用总数
nr_system_calls = 72

/*
 * Ok, I get parallel printer interrupts while using the floppy for some
 * strange reason. Urgel. Now I just ignore them.
 */
.globl _system_call,_sys_fork,_timer_interrupt,_sys_execve
.globl _hd_interrupt,_floppy_interrupt,_parallel_interrupt
.globl _device_not_available, _coprocessor_error

.align 2
#错误系统调用，返回-1
bad_sys_call:
	movl $-1,%eax
	iret
.align 2
#重新执行调度程序入口
reschedule:
	pushl $ret_from_sys_call
	jmp _schedule
.align 2
#系统调用入口，ax为系统调用号，这一部分不懂设置ds,es,fs的意图
#现在才明显的看明白了其实这里设置的是ds，es在GDT中的位置
#其中0x8为内核代码段,0x10为内核数据段,ox0f为任务段0x17为用户数据段
#所以设置ds,es记录内核数据段的地址fs为用户数据段的地址
_system_call:
	cmpl $nr_system_calls-1,%eax #比较有效系统调用的地址与ax寄存器的
	ja bad_sys_call     #对于非法的系统调用返回错误，ja指令为ax的值大于系统调用的最后一个认为是非法的系统调用
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx			# push %ebx,%ecx,%edx as parameters
	pushl %ebx			# to the system call
	movl $0x10,%edx		# set up ds,es to kernel space
	mov %dx,%ds
	mov %dx,%es
	movl $0x17,%edx		# 设置fs的值fs points to local data space fs寄存器指向本地的数据空间
	mov %dx,%fs
	call _sys_call_table(,%eax,4)	#系统调用
	pushl %eax			#系统调用返回值入栈
	movl _current,%eax
	cmpl $0,state(%eax)		#判断当前任务的状态是否为完成 state
	jne reschedule
	cmpl $0,counter(%eax)		#判断当前任务的时间片是否运行完成counter
	je reschedule
#系统调用返回处理
ret_from_sys_call:
	movl _current,%eax		# task[0] cannot have signals
	cmpl _task,%eax			#判断是否是任务0
	je 3f					#对于是任务0跳转到前面的标号3
	cmpw $0x0f,CS(%esp)		#判断是否是用户调用was old code segment supervisor ?
	jne 3f
	cmpw $0x17,OLDSS(%esp)	#判断栈段的值是否为17 was stack segment = 0x17 ?
	jne 3f
	movl signal(%eax),%ebx	#将信号值付给bx
	movl blocked(%eax),%ecx	#将进程信号屏蔽码赋给cx
	notl %ecx				#对cx进程非操作
	andl %ebx,%ecx			#进行且操作
	bsfl %ecx,%ecx			#获取从左到右扫描第一个1的位置存放在cx中
	je 3f					#
	btrl %ecx,%ebx			#对cx的值测试bx中的此位是否为1然后设置此位为1
	movl %ebx,signal(%eax) 	#设置任务的信号值
	incl %ecx				
	pushl %ecx
	call _do_signal			#调用信号处理程序
	popl %eax
3:	popl %eax
	popl %ebx
	popl %ecx
	popl %edx
	pop %fs
	pop %es
	pop %ds
	iret

.align 2
#协处理器错误
_coprocessor_error:
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax		
	mov %ax,%fs			#设置fs使fs的值指向本地数据
	pushl $ret_from_sys_call
	jmp _math_error

.align 2
#设备不可用或协处理器不在
_device_not_available:
	push %ds
	push %es
	push %fs
	pushl %edx
	pushl %ecx
	pushl %ebx
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	pushl $ret_from_sys_call
	clts						# clear TS so that we can use math
	movl %cr0,%eax
	testl $0x4,%eax				# EM (math emulation bit)
	je _math_state_restore
	pushl %ebp
	pushl %esi
	pushl %edi
	call _math_emulate
	popl %edi
	popl %esi
	popl %ebp
	ret

.align 2
#时间中断
_timer_interrupt:
	push %ds		# save ds,es and put kernel data space
	push %es		# into them. %fs is used by _system_call
	push %fs
	pushl %edx		# we save %eax,%ecx,%edx as gcc doesn't
	pushl %ecx		# save those across function calls. %ebx
	pushl %ebx		# is saved as we use that in ret_sys_call
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	incl _jiffies
	movb $0x20,%al		# EOI to interrupt controller #1
	outb %al,$0x20
	movl CS(%esp),%eax
	andl $3,%eax		# %eax is CPL (0 or 3, 0=supervisor)
	pushl %eax
	call _do_timer		# 'do_timer(long CPL)' does everything from
	addl $4,%esp		# task switching to accounting ...
	jmp ret_from_sys_call

.align 2
#系统调用
_sys_execve:
	lea EIP(%esp),%eax	#将IP指针指向的地址赋值给ax
	pushl %eax			#将源IP指针压栈
	call _do_execve		#调用此函数
	addl $4,%esp		#出栈一个参数
	ret					#返回

.align 2
#fork函数
_sys_fork:
	call _find_empty_process 	#调用寻找空闲的进程函数，即获取没有被使用的进程空间
	testl %eax,%eax				#
	js 1f
	push %gs
	pushl %esi
	pushl %edi
	pushl %ebp
	pushl %eax
	call _copy_process			#掉用进制拷贝
	addl $20,%esp
1:	ret
/**硬盘中断*/
_hd_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0xA0		# EOI to interrupt controller #1
	jmp 1f			# give port chance to breathe
1:	jmp 1f
1:	xorl %edx,%edx
	xchgl _do_hd,%edx
	testl %edx,%edx
	jne 1f
	movl $_unexpected_hd_interrupt,%edx
1:	outb %al,$0x20
	call *%edx		# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret
/**软盘中断*/
_floppy_interrupt:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	movl $0x17,%eax
	mov %ax,%fs
	movb $0x20,%al
	outb %al,$0x20		# EOI to interrupt controller #1
	xorl %eax,%eax
	xchgl _do_floppy,%eax
	testl %eax,%eax
	jne 1f
	movl $_unexpected_floppy_interrupt,%eax
1:	call *%eax			# "interesting" way of handling intr.
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret
/**并口中断处理，未实现*/
_parallel_interrupt:
	pushl %eax
	movb $0x20,%al
	outb %al,$0x20
	popl %eax
	iret
