/*
 *  linux/mm/page.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * page.s contains the low-level page-exception code.
 * the real work is done in mm.c
 */

.globl _page_fault
/*页也默认*/
_page_fault:
	xchgl %eax,(%esp)  #交换值，ax存储sp地址对应的值
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%edx
	mov %dx,%ds
	mov %dx,%es
	mov %dx,%fs
	movl %cr2,%edx  #将cr2寄存器的值赋给dx
	pushl %edx
	pushl %eax
	testl $1,%eax
	jne 1f
	call _do_no_page
	jmp 2f
1:	call _do_wp_page
2:	addl $8,%esp
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret
