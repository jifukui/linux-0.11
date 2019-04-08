/*
 *  linux/kernel/blk_dev/ll_rw.c
 *
 * (C) 1991 Linus Torvalds
 */

/*
 * This handles all read/write requests to block devices
 */
#include <errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <asm/system.h>

#include "blk.h"

/*
 * The request-struct contains all necessary data
 * to load a nr of sectors into memory
 */
struct request request[NR_REQUEST];

/*
 * used to wait on when there are no free requests
 */
struct task_struct * wait_for_request = NULL;

/* blk_dev_struct is:
 *	do_request-address
 *	next-request
 */
struct blk_dev_struct blk_dev[NR_BLK_DEV] = {
	{ NULL, NULL },		/* no_dev */
	{ NULL, NULL },		/* dev mem */
	{ NULL, NULL },		/* dev fd */
	{ NULL, NULL },		/* dev hd */
	{ NULL, NULL },		/* dev ttyx */
	{ NULL, NULL },		/* dev tty */
	{ NULL, NULL }		/* dev lp */
};

/**锁定缓冲区*/
static inline void lock_buffer(struct buffer_head * bh)
{
	/**关闭中断*/
	cli();
	/**直到此缓冲区解锁否则睡眠*/
	while (bh->b_lock)
	{
		sleep_on(&bh->b_wait);
	}
	/**锁定此缓冲区*/
	bh->b_lock=1;
	/**打开中断*/
	sti();
}

/**解锁缓冲区*/
static inline void unlock_buffer(struct buffer_head * bh)
{
	/**如果缓冲区未锁定内核输出*/
	if (!bh->b_lock)
	{
		printk("ll_rw_block.c: buffer not locked\n\r");	
	}
	/**设置缓冲区未锁定*/
	bh->b_lock = 0;
	/**唤醒等待此缓冲区的进程*/
	wake_up(&bh->b_wait);
}

/*
 * add-request adds a request to the linked list.
 * It disables interrupts so that it can muck with the
 * request-lists in peace.
 */
/**向请求队列中添加请求*/
static void add_request(struct blk_dev_struct * dev, struct request * req)
{
	struct request * tmp;

	req->next = NULL;
	cli();
	/**对于请求指向的缓冲区头表使用处理
	 * 设置未写
	*/
	if (req->bh)
	{
		req->bh->b_dirt = 0;
	}
	/**对于块设备结构为0的处理
	 * 及当前设备是否有当前请求项
	 * 设置指定设备的当前请求项为传入的请求项
	 * 设置打开中断
	 * 调用请求项的处理函数
	*/
	if (!(tmp = dev->current_request)) 
	{
		dev->current_request = req;
		sti();
		(dev->request_fn)();
		return;
	}
	/**调用电梯算法优化磁盘读取*/
	for ( ; tmp->next ; tmp=tmp->next)
	{
		if ((IN_ORDER(tmp,req) ||!IN_ORDER(tmp,tmp->next)) &&IN_ORDER(req,tmp->next))
		{
			break;
		}
	}
	req->next=tmp->next;
	tmp->next=req;
	/**设置打开中断*/
	sti();
}

/**产生请求*/
static void make_request(int major,int rw, struct buffer_head * bh)
{
	struct request * req;
	int rw_ahead;

/* WRITEA/READA is special case - it is not really needed, so if the */
/* buffer is locked, we just forget about it, else it's a normal read */
	/**对于操作为读写操作的处理*/
	if (rw_ahead = (rw == READA || rw == WRITEA)) 
	{
		/**如果缓冲区被锁定直接返回*/
		if (bh->b_lock)
		{
			return;
		}
		/**设置读写操作为读*/
		if (rw == READA)
		{
			rw = READ;
		}
		/**设置读写操作为写*/
		else
		{
			rw = WRITE;
		}
	}
	/**对于非读写操作的处理
	 * 内核输出错误
	*/
	if (rw!=READ && rw!=WRITE)
	{
		panic("Bad block dev command, must be R/W/RA/WA");
	}
	/**锁定此缓冲区*/
	lock_buffer(bh);
	/**对于为写操作且没有写过数据或者是当前操作为读操作且当前没有更新的操作*/
	if ((rw == WRITE && !bh->b_dirt) || (rw == READ && bh->b_uptodate)) 
	{
		/**解除缓冲区锁定并返回*/
		unlock_buffer(bh);
		return;
	}
repeat:
/* we don't allow the write-requests to fill up the queue completely:
 * we want some room for reads: they take precedence. The last third
 * of the requests are only for reads.
 */
	/**对于读操作设置请求队列的位置*/
	if (rw == READ)
	{
		req = request+NR_REQUEST;
	}
	/**对于写操作设置请求队列的位置*/
	else
	{
		req = request+((NR_REQUEST*2)/3);
	}
/* find an empty request */
	/**寻找空闲的请求项*/
	while (--req >= request)
	{
		if (req->dev<0)
		{
			break;
		}
	}
/* if none found, sleep on new requests: check for rw_ahead */
	/**如果没有找到空闲的请求项的处理*/
	if (req < request) 
	{
		if (rw_ahead) 
		{
			unlock_buffer(bh);
			return;
		}
		sleep_on(&wait_for_request);
		goto repeat;
	}
/* fill up the request-info, and add it to the queue */
	/**填充请求项参数*/
	req->dev = bh->b_dev;
	req->cmd = rw;
	req->errors=0;
	req->sector = bh->b_blocknr<<1;
	req->nr_sectors = 2;
	req->buffer = bh->b_data;
	req->waiting = NULL;
	req->bh = bh;
	req->next = NULL;
	/**添加到请求队列中*/
	add_request(major+blk_dev,req);
}
/**底层读写数据块函数
 * 主要是调用make_request函数
*/
void ll_rw_block(int rw, struct buffer_head * bh)
{
	unsigned int major;

	if ((major=MAJOR(bh->b_dev)) >= NR_BLK_DEV ||!(blk_dev[major].request_fn)) 
	{
		printk("Trying to read nonexistent block-device\n\r");
		return;
	}
	make_request(major,rw,bh);
}
/**块设备初始化函数*/
void blk_dev_init(void)
{
	int i;
	/**初始化NR_REQUEST（32）个请求队列的值*/
	for (i=0 ; i<NR_REQUEST ; i++) 
	{
		request[i].dev = -1;
		request[i].next = NULL;
	}
}
