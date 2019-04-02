/*
 *  linux/kernel/asm.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * asm.s contains the low-level code for most hardware faults.
 * page_exception is handled by the mm, so that isn't here. This
 * file also handles (hopefully) fpu-exceptions due to TS-bit, as
 * the fpu must be properly saved/resored. This hasn't been tested.
 */

.globl _divide_error,_debug,_nmi,_int3,_overflow,_bounds,_invalid_op
.globl _double_fault,_coprocessor_segment_overrun
.globl _invalid_TSS,_segment_not_present,_stack_segment
.globl _general_protection,_coprocessor_error,_irq13,_reserved
/**除法错误处理，除0错误*/
_divide_error:
	pushl $_do_divide_error	#
/**无出错号处理*/
no_error_code:
	xchgl %eax,(%esp)	#ax存储函数的地址 交换指令，讲ax寄存器的值与sp寄存器指向的地址的值进行交换
	pushl %ebx			#bx存储函数的返回值
	pushl %ecx			#cx
	pushl %edx			#dx
	pushl %edi			#di
	pushl %esi			#si
	pushl %ebp			#bp
	push %ds			#ds
	push %es			#es
	push %fs			#fs
	pushl $0			# 错误码"error code"
	lea 44(%esp),%edx 	#dx存储的是上一次sp的位置，将esp寄存器的加上44存入edx寄存器中
	pushl %edx			#
	movl $0x10,%edx		#
	mov %dx,%ds			#设置数据段的值
	mov %dx,%es			#设置扩展段的值
	mov %dx,%fs			#设置扩展段的值
	call *%eax			#调用函数
	addl $8,%esp		#跳过sp的值和错误码的值在堆栈中的位置
	pop %fs				#
	pop %es				#		
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret
/**debug处理*/
_debug:
	pushl $_do_int3		# _do_debug
	jmp no_error_code
/**非屏蔽中断处理*/
_nmi:
	pushl $_do_nmi
	jmp no_error_code
/**int3处理*/
_int3:
	pushl $_do_int3
	jmp no_error_code
/**溢出处理*/
_overflow:
	pushl $_do_overflow
	jmp no_error_code
/***/
_bounds:
	pushl $_do_bounds
	jmp no_error_code
/**非法指令处理*/
_invalid_op:
	pushl $_do_invalid_op
	jmp no_error_code
/**段错误处理*/
_coprocessor_segment_overrun:
	pushl $_do_coprocessor_segment_overrun
	jmp no_error_code
/**预留错误处理*/
_reserved:
	pushl $_do_reserved
	jmp no_error_code
/***/
_irq13:
	pushl %eax
	xorb %al,%al
	outb %al,$0xF0
	movb $0x20,%al
	outb %al,$0x20
	jmp 1f
1:	jmp 1f
1:	outb %al,$0xA0
	popl %eax
	jmp _coprocessor_error
/**默认错误*/
_double_fault:
	pushl $_do_double_fault
error_code:
	xchgl %eax,4(%esp)		# ax中存放错误码error code <-> %eax 其中的括号表示去此寄存器存储的值
	xchgl %ebx,(%esp)		# bx中存放函数地址&function <-> %ebx
	pushl %ecx
	pushl %edx
	pushl %edi
	pushl %esi
	pushl %ebp
	push %ds
	push %es
	push %fs
	pushl %eax			# error code
	lea 44(%esp),%eax		# offset
	pushl %eax
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	call *%ebx
	addl $8,%esp
	pop %fs
	pop %es
	pop %ds
	popl %ebp
	popl %esi
	popl %edi
	popl %edx
	popl %ecx
	popl %ebx
	popl %eax
	iret
/**非法的TSS*/
_invalid_TSS:
	pushl $_do_invalid_TSS
	jmp error_code
/**段不存在*/
_segment_not_present:
	pushl $_do_segment_not_present
	jmp error_code
/**栈段*/
_stack_segment:
	pushl $_do_stack_segment
	jmp error_code
/**通用保护*/
_general_protection:
	pushl $_do_general_protection
	jmp error_code

