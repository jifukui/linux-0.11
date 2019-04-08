#ifndef _BLK_H
#define _BLK_H

#define NR_BLK_DEV	7
/*
 * NR_REQUEST is the number of entries in the request-queue.
 * NOTE that writes may use only the low 2/3 of these: reads
 * take precedence.
 *
 * 32 seems to be a reasonable number: enough to get some benefit
 * from the elevator-mechanism, but not so much as to lock a lot of
 * buffers when they are in the queue. 64 seems to be too many (easily
 * long pauses in reading when heavy writing/syncing is going on)
 */
#define NR_REQUEST	32

/*
 * Ok, this is an expanded form so that we can use the same
 * request for paging requests when that is implemented. In
 * paging, 'bh' is NULL, and 'waiting' is used to wait for
 * read/write completion.
 */
 /**请求数据结构
  * dev：设备号
  * cmd：读写命令
  * errors：操作时产生的错误次数
  * sector：起始扇区
  * nr_sectors：扇区数量
  * buffer：数据缓冲区
  * waiting：任务等待操作执行完成的地方
  * bh：缓冲区头指针
  * next:下一个请求指针
 */
struct request {
	int dev;		/* -1 if no request */
	int cmd;		/* READ or WRITE */
	int errors;
	unsigned long sector;
	unsigned long nr_sectors;
	char * buffer;
	struct task_struct * waiting;
	struct buffer_head * bh;
	struct request * next;
};

/*
 * This is used in the elevator algorithm: Note that
 * reads always go before writes. This is natural: reads
 * are much more time-critical than writes.
 */
#define IN_ORDER(s1,s2) \
((s1)->cmd<(s2)->cmd || (s1)->cmd==(s2)->cmd && \
((s1)->dev < (s2)->dev || ((s1)->dev == (s2)->dev && \
(s1)->sector < (s2)->sector)))
/**块设备结构
 * request_fn函数指针指向调用的函数
 * current_request当前请求项指针
*/
struct blk_dev_struct {
	void (*request_fn)(void);
	struct request * current_request;
};

extern struct blk_dev_struct blk_dev[NR_BLK_DEV];
extern struct request request[NR_REQUEST];
extern struct task_struct * wait_for_request;

#ifdef MAJOR_NR

/*
 * Add entries as needed. Currently the only block devices
 * supported are hard-disks and floppies.
 */

#if (MAJOR_NR == 1)
/* ram disk  ram盘 */
#define DEVICE_NAME "ramdisk"
#define DEVICE_REQUEST do_rd_request
#define DEVICE_NR(device) ((device) & 7)
#define DEVICE_ON(device) 
#define DEVICE_OFF(device)

#elif (MAJOR_NR == 2)
/* floppy 软盘*/
#define DEVICE_NAME "floppy"
#define DEVICE_INTR do_floppy
#define DEVICE_REQUEST do_fd_request
#define DEVICE_NR(device) ((device) & 3)
#define DEVICE_ON(device) floppy_on(DEVICE_NR(device))
#define DEVICE_OFF(device) floppy_off(DEVICE_NR(device))

#elif (MAJOR_NR == 3)
/* harddisk 硬盘*/
#define DEVICE_NAME "harddisk"
#define DEVICE_INTR do_hd
#define DEVICE_REQUEST do_hd_request
#define DEVICE_NR(device) (MINOR(device)/5)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif
/* unknown blk device */
#error "unknown blk device"

#endif

#define CURRENT (blk_dev[MAJOR_NR].current_request)
#define CURRENT_DEV DEVICE_NR(CURRENT->dev)

#ifdef DEVICE_INTR
void (*DEVICE_INTR)(void) = NULL;
#endif
static void (DEVICE_REQUEST)(void);
/**解锁指定的缓冲区*/
extern inline void unlock_buffer(struct buffer_head * bh)
{
	/**对于未上锁的缓冲区的处理
	 * 内核打印输出信息
	*/
	if (!bh->b_lock)
	{
		printk(DEVICE_NAME ": free buffer being unlocked\n");
	}
	/**设置此缓冲区未上锁*/
	bh->b_lock=0;
	/**唤醒*/
	wake_up(&bh->b_wait);
}
/**最后一个请求的处理
 * 
*/
extern inline void end_request(int uptodate)
{
	/**关闭设备*/
	DEVICE_OFF(CURRENT->dev);
	/**判断当前进程的缓冲区头部是否有值*/
	if (CURRENT->bh) 
	{
		/**设置当前缓冲区的更新标志*/
		CURRENT->bh->b_uptodate = uptodate;
		/**设置当前缓冲区解锁*/
		unlock_buffer(CURRENT->bh);
	}
	/**对于更新标志为0的处理，
	 * 更新标志为0表示当前操作失败
	*/
	if (!uptodate) 
	{
		/**内核输出相关信息*/
		printk(DEVICE_NAME " I/O error\n\r");
		printk("dev %04x, block %d\n\r",CURRENT->dev,
			CURRENT->bh->b_blocknr);
	}
	/**唤醒等待该请求项的进程*/
	wake_up(&CURRENT->waiting);
	/**唤醒等待空闲请求项出现的进程*/
	wake_up(&wait_for_request);
	/**设置当前进程的设备为-1*/
	CURRENT->dev = -1;
	/**设置当前进程，指向下一个进程*/
	CURRENT = CURRENT->next;
}

#define INIT_REQUEST \
repeat: \
	if (!CURRENT) \
		return; \
	if (MAJOR(CURRENT->dev) != MAJOR_NR) \
		panic(DEVICE_NAME ": request list destroyed"); \
	if (CURRENT->bh) { \
		if (!CURRENT->bh->b_lock) \
			panic(DEVICE_NAME ": block not locked"); \
	}

#endif

#endif
