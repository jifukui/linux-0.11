/*
 *  linux/boot/head.s
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 *  head.s contains the 32-bit startup code.
 *
 * NOTE!!! Startup happens at absolute address 0x00000000, which is also where
 * the page directory will exist. The startup code will be overwritten by
 * the page directory.
 */
.text
.globl _idt,_gdt,_pg_dir,_tmp_floppy_area
/*页目录存放位置*/
_pg_dir:
startup_32:
	/*指向数据段，在段模式下CS为代码段，DS为数据段,SS为栈段，ED,FS,GS为附加段指针*/
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	mov %ax,%gs
	/*设置堆栈指针的位置*/
	lss _stack_start,%esp
	/*调用建立idt和gdt*/
	call setup_idt
	call setup_gdt
	/*再次设置段寄存器*/
	movl $0x10,%eax		# reload all the segment registers
	mov %ax,%ds			# after changing gdt. CS was already
	mov %ax,%es			# reloaded in 'setup_gdt'
	mov %ax,%fs
	mov %ax,%gs
	/**设置堆寄存器的起始位置*/
	lss _stack_start,%esp
	/**判断内存0处与内存0x100000处的值是否一致如果一致表示A20没有开启进入死循环*/
	xorl %eax,%eax		#异或
1:	incl %eax			# 加一 check that A20 really IS enabled
	movl %eax,0x000000	# 将ax寄存器中的数据写入内存0x0处 loop forever if it isn't
	cmpl %eax,0x100000	#判断地址0x100000处的数据是否也为此值
	je 1b
/*
 * NOTE! 486 should set bit 16, to check for write-protect in supervisor
 * mode. Then it would be unnecessary with the "verify_area()"-calls.
 * 486 users probably want to set the NE (#5) bit also, so as to use
 * int 16 for math errors.
 */
	/**检测数字协处理器是否存在*/
	movl %cr0,%eax		# check math chip
	andl $0x80000011,%eax	#获取协处理器的部分值 Save PG,PE,ET
/* "orl $0x10020,%eax" here for 486 might be good */
	orl $2,%eax		#进行或运算 set MP
	movl %eax,%cr0	#将ax寄存器中的值写入到cr0寄存器中
	call check_x87	#调用检测是否存在协处理器
	jmp after_page_tables

/*
 * We depend on ET to be correct. This checks for 287/387.
 */
 /**判断协处理器*/
check_x87:
	fninit
	fstsw %ax
	cmpb $0,%al
	je 1f			/* 如果不存在协处理器调到前面的1标号处的程序no coprocessor: have to set bits */
	movl %cr0,%eax
	xorl $6,%eax		/* reset MP, set EM */
	movl %eax,%cr0	#设置cr寄存器
	ret				#调回到call check_x87处
.align 2
1:	.byte 0xDB,0xE4		/* fsetpm for 287, ignored by 387 */
	ret					#调回到call check_x87处

/*
 *  setup_idt
 *
 *  sets up a idt with 256 entries pointing to
 *  ignore_int, interrupt gates. It then loads
 *  idt. Everything that wants to install itself
 *  in the idt-table may do so themselves. Interrupts
 *  are enabled elsewhere, when we can be relatively
 *  sure everything is ok. This routine will be over-
 *  written by the page tables.
 */
 /**设置中断描述符表*/
setup_idt:
	#将ignore_int的地址装入dx寄存器中
	lea ignore_int,%edx
	#设置ax寄存器的值
	movl $0x00080000,%eax
	movw %dx,%ax		/* selector = 0x0008 = cs */
	movw $0x8E00,%dx	/* interrupt gate - dpl=0, present */

	lea _idt,%edi
	mov $256,%ecx
rp_sidt:
	movl %eax,(%edi)
	movl %edx,4(%edi) #此处的意思是将dx的值写入到di+4处
	addl $8,%edi
	dec %ecx
	jne rp_sidt
	lidt idt_descr    #加载中断描述表
	ret

/*
 *  setup_gdt
 *
 *  This routines sets up a new gdt and loads it.
 *  Only two entries are currently built, the same
 *  ones that were built in init.s. The routine
 *  is VERY complicated at two whole lines, so this
 *  rather long comment is certainly needed :-).
 *  This routine will beoverwritten by the page tables.
 */
setup_gdt:
	lgdt gdt_descr
	ret

/*
 * I put the kernel page tables right after the page directory,
 * using 4 of them to span 16 Mb of physical memory. People with
 * more than 16MB will have to expand this.
 */
.org 0x1000 #设置相对内存的偏移位置为0x1000的值
pg0:

.org 0x2000
pg1:

.org 0x3000
pg2:

.org 0x4000
pg3:

.org 0x5000
/*
 * tmp_floppy_area is used by the floppy-driver when DMA cannot
 * reach to a buffer-block. It needs to be aligned, so that it isn't
 * on a 64kB border.
 */
 #设置相对于内存0x5000处的内容，
_tmp_floppy_area:
	.fill 1024,1,0 #填充1024项每项1字节填充值为0

after_page_tables:
	pushl $0		# envpThese are the parameters to main :-)
	pushl $0		#argv
	pushl $0		#argc
	pushl $L6		#返回地址 return address for main, if it decides to.
	pushl $_main	#程序地址
	jmp setup_paging #跳转到setup_paging处
L6:
	jmp L6			# main should never return here, but
				# just in case, we know what happens.

/* This is the default interrupt "handler" :-) */
/**下面是默认中断向量句柄*/
int_msg:
	.asciz "Unknown interrupt\n\r"
.align 2
ignore_int:
	pushl %eax
	pushl %ecx
	pushl %edx
	push %ds
	push %es
	push %fs
	movl $0x10,%eax
	mov %ax,%ds
	mov %ax,%es
	mov %ax,%fs
	pushl $int_msg
	call _printk
	popl %eax
	pop %fs
	pop %es
	pop %ds
	popl %edx
	popl %ecx
	popl %eax
	iret


/*
 * Setup_paging
 *
 * This routine sets up paging by setting the page bit
 * in cr0. The page tables are set up, identity-mapping
 * the first 16MB. The pager assumes that no illegal
 * addresses are produced (ie >4Mb on a 4Mb machine).
 *
 * NOTE! Although all physical memory should be identity
 * mapped by this routine, only the kernel page functions
 * use the >1Mb addresses directly. All "normal" functions
 * use just the lower 1Mb, or the local data space, which
 * will be mapped to some other place - mm keeps track of
 * that.
 *
 * For those with more memory than 16 Mb - tough luck. I've
 * not got it, why should you :-) The source is here. Change
 * it. (Seriously - it shouldn't be too difficult. Mostly
 * change some constants etc. I left it at 16Mb, as my machine
 * even cannot be extended past that (ok, but it was cheap :-)
 * I've tried to show which constants to change by having
 * some kind of marker at them (search for "16Mb"), but I
 * won't guarantee that's all :-( )
 */
.align 2
setup_paging:
	movl $1024*5,%ecx		/* 计算5页的字节长度5 pages - pg_dir+4 page tables */
	xorl %eax,%eax			#设置ax寄存器的值为0
	xorl %edi,%edi			/*设置di寄存器的值为0 pg_dir is at 0x000 */
	cld;					#设置si和di的方向为递增方向
	rep;					#设置重复次数
	stosl					#设置美的di加4
	#填写页目录表的参数
	movl $pg0+7,_pg_dir		/* set present bit/user r/w */
	movl $pg1+7,_pg_dir+4		/*  --------- " " --------- */
	movl $pg2+7,_pg_dir+8		/*  --------- " " --------- */
	movl $pg3+7,_pg_dir+12		/*  --------- " " --------- */
	#设置4个页表的内容
	movl $pg3+4092,%edi
	movl $0xfff007,%eax		/*  16Mb - 4096 + 7 (r/w user,p) */
	std				#设置si和di的方向为递减方向
1:	stosl			/* fill pages backwards - more efficient :-) */
	subl $0x1000,%eax
	jge 1b
	xorl %eax,%eax		/* pg_dir is at 0x0000 */
	movl %eax,%cr3		/*设置cr3保存页目录表的地址 cr3 - page directory start */
	movl %cr0,%eax
	orl $0x80000000,%eax
	movl %eax,%cr0		/* set paging (PG) bit */
	ret			/* this also flushes prefetch-queue */

.align 2
.word 0
idt_descr:
	.word 256*8-1		#表长度 idt contains 256 entries
	.long _idt			#表线性地址
.align 2
.word 0
gdt_descr:
	.word 256*8-1		# so does gdt (not that that's any
	.long _gdt		# magic number, but it works for me :^)

	.align 3
_idt:	.fill 256,8,0		# idt is uninitialized

_gdt:	.quad 0x0000000000000000	/* NULL descriptor */
	.quad 0x00c09a0000000fff	/* 16Mb */
	.quad 0x00c0920000000fff	/* 16Mb */
	.quad 0x0000000000000000	/* TEMPORARY - don't use */
	.fill 252,8,0			/* space for LDT's and TSS's etc */
