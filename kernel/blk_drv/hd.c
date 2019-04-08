/*
 *  linux/kernel/hd.c
 *
 *  (C) 1991  Linus Torvalds
 */

/*
 * This is the low-level hd interrupt support. It traverses the
 * request-list, using interrupts to jump between functions. As
 * all the functions are called within interrupts, we may not
 * sleep. Special care is recommended.
 * 
 *  modified by Drew Eckhardt to check nr of hd's from the CMOS.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>
/**定义硬盘设备的主设备号为3*/
#define MAJOR_NR 3
#include "blk.h"

#define CMOS_READ(addr) ({ \
outb_p(0x80|addr,0x70); \
inb_p(0x71); \
})

/* Max read/write errors/sector */
#define MAX_ERRORS	7
#define MAX_HD		2

static void recal_intr(void);

static int recalibrate = 1;
static int reset = 1;

/*
 *  This struct defines the HD's and their types.
 */
/**硬盘信息数据结构
 * head：磁头数
 * sect：每磁道扇区数
 * cyl：柱面数
 * wpcom：写前预补偿柱面号
 * lzone：磁头着陆区柱面号
 * ctl：控制字节
*/
struct hd_i_struct {
	int head;
	int sect;
	int cyl;
	int wpcom;
	int lzone;
	int ctl;
	};
#ifdef HD_TYPE
struct hd_i_struct hd_info[] = { HD_TYPE };
#define NR_HD ((sizeof (hd_info))/(sizeof (struct hd_i_struct)))
#else
struct hd_i_struct hd_info[] = { {0,0,0,0,0,0},{0,0,0,0,0,0} };
static int NR_HD = 0;
#endif
/**硬盘结构体
 * start_sect：起始扇区
 * nr_sects：扇区数量
*/
static struct hd_struct {
	long start_sect;
	long nr_sects;
} hd[5*MAX_HD]={{0,0},};
/**读数据*/
#define port_read(port,buf,nr) \
__asm__("cld;rep;insw"::"d" (port),"D" (buf),"c" (nr):"cx","di")
/**写数据*/
#define port_write(port,buf,nr) \
__asm__("cld;rep;outsw"::"d" (port),"S" (buf),"c" (nr):"cx","si")

extern void hd_interrupt(void);
extern void rd_load(void);

/* This may be used only once, enforced by 'static int callable' */
/**系统设置函数
 * 此函数只被调用一次
 * callable静态变量标识此函数是否可以调用
 * 
*/
int sys_setup(void * BIOS)
{
	static int callable = 1;
	int i,drive;
	unsigned char cmos_disks;
	struct partition *p;
	struct buffer_head * bh;

	if (!callable)
	{
		return -1;
	}
	callable = 0;
#ifndef HD_TYPE
	/**如果未定义HD_TYPE则初始化硬盘信息*/
	for (drive=0 ; drive<2 ; drive++) 
	{
		hd_info[drive].cyl = *(unsigned short *) BIOS;
		hd_info[drive].head = *(unsigned char *) (2+BIOS);
		hd_info[drive].wpcom = *(unsigned short *) (5+BIOS);
		hd_info[drive].ctl = *(unsigned char *) (8+BIOS);
		hd_info[drive].lzone = *(unsigned short *) (12+BIOS);
		hd_info[drive].sect = *(unsigned char *) (14+BIOS);
		BIOS += 16;
	}
	if (hd_info[1].cyl)
	{
		NR_HD=2;
	}
	else
	{
		NR_HD=1;
	}
#endif
	/**硬盘的第0和5项为硬盘的整体信息
	 * 硬盘的1~4和6~9项表示4个分区的参数
	*/
	for (i=0 ; i<NR_HD ; i++) 
	{
		hd[i*5].start_sect = 0;
		hd[i*5].nr_sects = hd_info[i].head*hd_info[i].sect*hd_info[i].cyl;
	}

	/*
		We querry CMOS about hard disks : it could be that 
		we have a SCSI/ESDI/etc controller that is BIOS
		compatable with ST-506, and thus showing up in our
		BIOS table, but not register compatable, and therefore
		not present in CMOS.

		Furthurmore, we will assume that our ST-506 drives
		<if any> are the primary drives in the system, and 
		the ones reflected as drive 1 or 2.

		The first drive is stored in the high nibble of CMOS
		byte 0x12, the second in the low nibble.  This will be
		either a 4 bit drive type or 0xf indicating use byte 0x19 
		for an 8 bit type, drive 1, 0x1a for drive 2 in CMOS.

		Needless to say, a non-zero value means we have 
		an AT controller hard disk for that drive.

		
	*/
	/**对硬盘的信息进行验证*/
	if ((cmos_disks = CMOS_READ(0x12)) & 0xf0)
	{
		if (cmos_disks & 0x0f)
		{
			NR_HD = 2;
		}
		else
		{
			NR_HD = 1;
		}
	}
	else
	{
		NR_HD = 0;
	}
	/**对不存在的硬盘进行数据清除*/
	for (i = NR_HD ; i < 2 ; i++) 
	{
		hd[i*5].start_sect = 0;
		hd[i*5].nr_sects = 0;
	}
	/**校验磁盘的有效性，对于不存在的磁盘进程释放内存*/
	for (drive=0 ; drive<NR_HD ; drive++) 
	{
		/**根据输入的设备号读取硬盘信息，300/305,返回0表示读取失败*/
		if (!(bh = bread(0x300 + drive*5,0))) 
		{
			printk("Unable to read partition table of drive %d\n\r",drive);
			panic("");
		}
		/**判断是否是有效的*/
		if (bh->b_data[510] != 0x55 || (unsigned char)bh->b_data[511] != 0xAA) 
		{
			printk("Bad partition table on drive %d\n\r",drive);
			panic("");
		}
		/**获取磁盘的起始扇区和扇区数量*/
		p = 0x1BE + (void *)bh->b_data;
		for (i=1;i<5;i++,p++) 
		{
			hd[i+5*drive].start_sect = p->start_sect;
			hd[i+5*drive].nr_sects = p->nr_sects;
		}
		/**释放申请的缓存区*/
		brelse(bh);
	}
	if (NR_HD)
	{
		printk("Partition table%s ok.\n\r",(NR_HD>1)?"s":"");
	}
	/**安装虚拟磁盘*/
	rd_load();
	/**挂载根文件系统*/
	mount_root();
	return (0);
}

/**等待硬盘控制器准备就绪*/
static int controller_ready(void)
{
	int retries=10000;

	while (--retries && (inb_p(HD_STATUS)&0xc0)!=0x40);
	return (retries);
}
/**检测硬盘执行命令后的状态*/
static int win_result(void)
{
	int i=inb_p(HD_STATUS);

	if ((i & (BUSY_STAT | READY_STAT | WRERR_STAT | SEEK_STAT | ERR_STAT))== (READY_STAT | SEEK_STAT))
	{
		return(0); /* ok */
	}
	if (i&1)
	{
		i=inb(HD_ERROR);
	}
	return (1);
}
/**向硬盘输出命令*/
static void hd_out(unsigned int drive,
					unsigned int nsect,
					unsigned int sect,
					unsigned int head,
					unsigned int cyl,
					unsigned int cmd,
					void (*intr_addr)(void)
					)
{
	/**定义局部寄存器变量，并存放在dx寄存器中*/
	register int port asm("dx");
	/**判断读取的扇区是否正确*/
	if (drive>1 || head>15)
	{
		panic("Trying to write bad sector");
	}
	/**判断硬盘是否准备就绪*/
	if (!controller_ready())
	{
		panic("HD controller not ready");
	}
	/**中断处理函数*/
	do_hd = intr_addr;
	/**输出控制字节*/
	outb_p(hd_info[drive].ctl,HD_CMD);
	/**设置dx为数据寄存器端口*/
	port=HD_DATA;
	/**写预补偿*/
	outb_p(hd_info[drive].wpcom>>2,++port);
	/**磁盘数*/
	outb_p(nsect,++port);
	/**起始扇区*/
	outb_p(sect,++port);
	/**柱面低8位*/
	outb_p(cyl,++port);
	/**柱面高8位*/
	outb_p(cyl>>8,++port);
	/**驱动器号，磁头号*/
	outb_p(0xA0|(drive<<4)|head,++port);
	/**硬盘控制命令*/
	outb(cmd,++port);
}
/**设备忙状态检测
 * 返回0表示设备可用
 * 返回1表示设备忙状态
*/
static int drive_busy(void)
{
	unsigned int i;

	for (i = 0; i < 10000; i++)
	{
		if (READY_STAT == (inb_p(HD_STATUS) & (BUSY_STAT|READY_STAT)))
		{
			break;
		}
	}
	i = inb(HD_STATUS);
	i &= BUSY_STAT | READY_STAT | SEEK_STAT;
	if (i == READY_STAT | SEEK_STAT)
	{
		return(0);
	}
	printk("HD controller times out\n\r");
	return(1);
}
/**重置控制器*/
static void reset_controller(void)
{
	int	i;
	/**4表示设备控制寄存器复位*/
	outb(4,HD_CMD);
	for(i = 0; i < 100; i++)
	{
		nop();
	}
	/**发送正常指令*/
	outb(hd_info[0].ctl & 0x0f ,HD_CMD);
	/**输出硬盘消息*/
	if (drive_busy())
	{
		printk("HD-controller still busy\n\r");
	}
	if ((i = inb(HD_ERROR)) != 1)
	{
		printk("HD-controller reset failed: %02x\n\r",i);
	}
}
/**重置硬盘*/
static void reset_hd(int nr)
{
	/**复位控制器*/
	reset_controller();
	/***/
	hd_out(nr,hd_info[nr].sect,hd_info[nr].sect,hd_info[nr].head-1,hd_info[nr].cyl,WIN_SPECIFY,&recal_intr);
}
/**意外中断调用处理函数*/
void unexpected_hd_interrupt(void)
{
	printk("Unexpected HD interrupt\n\r");
}
/**读写失败调用函数*/
static void bad_rw_intr(void)
{
	/**失败次数超过定义的最大失败次数处理
	 * 设置最后请求失败处理
	*/
	if (++CURRENT->errors >= MAX_ERRORS)
	{
		end_request(0);
	}
	/**失败次数大于定义的最大失败次数的一半*/
	if (CURRENT->errors > MAX_ERRORS/2)
	{
		reset = 1;
	}
}
/**读中断处理函数*/
static void read_intr(void)
{
	/**判断硬盘执行命令后的状态
	 * 对于异常直接退出
	*/
	if (win_result()) 
	{
		/**读写失败处理*/
		bad_rw_intr();
		do_hd_request();
		return;
	}
	/**读端口数据 512字节*/
	port_read(HD_DATA,CURRENT->buffer,256);
	CURRENT->errors = 0;
	CURRENT->buffer += 512;
	CURRENT->sector++;
	/**判断读扇区是否是最后一个扇区
	 * 如果不是继续调用读处理函数
	*/
	if (--CURRENT->nr_sectors) 
	{
		do_hd = &read_intr;
		return;
	}
	/**最后一个请求处理*/
	end_request(1);
	/***/
	do_hd_request();
}
/**写中断处理函数*/
static void write_intr(void)
{
	/**判断硬盘执行命令后的状态
	 * 对于异常直接退出
	*/
	if (win_result()) 
	{
		bad_rw_intr();
		do_hd_request();
		return;
	}
	/**判断是不是写的最后一个扇区
	 * 对于不是最后一个扇区设计继续写
	*/
	if (--CURRENT->nr_sectors) 
	{
		CURRENT->sector++;
		CURRENT->buffer += 512;
		do_hd = &write_intr;
		port_write(HD_DATA,CURRENT->buffer,256);
		return;
	}
	/**最后一个扇区处理函数*/
	end_request(1);
	/***/
	do_hd_request();
}
/**硬盘重新矫正处理函数*/
static void recal_intr(void)
{
	/**获取硬盘处理状态*/
	if (win_result())
	{
		bad_rw_intr();
	}
	/**再次调用处理函数*/
	do_hd_request();
}
/**执行硬盘请求*/
void do_hd_request(void)
{
	int i,r;
	unsigned int block,dev;
	unsigned int sec,head,cyl;
	unsigned int nsect;

	INIT_REQUEST;
	/**获取设备号*/
	dev = MINOR(CURRENT->dev);
	/**获取起始扇区*/
	block = CURRENT->sector;
	/**对于错误扇区和设备号的处理*/
	if (dev >= 5*NR_HD || block+2 > hd[dev].nr_sects) 
	{
		end_request(0);
		goto repeat;
	}
	/**设置block为*/
	block += hd[dev].start_sect;
	dev /= 5;
	__asm__("divl %4":"=a" (block),"=d" (sec):"0" (block),"1" (0),
		"r" (hd_info[dev].sect));
	__asm__("divl %4":"=a" (cyl),"=d" (head):"0" (block),"1" (0),
		"r" (hd_info[dev].head));
	sec++;
	/**设置扇区数量*/
	nsect = CURRENT->nr_sectors;
	/**对于reset为1的处理*/
	if (reset) 
	{
		reset = 0;
		recalibrate = 1;
		/**服务当前设备*/
		reset_hd(CURRENT_DEV);
		return;
	}
	/**对于设置矫正标志位的处理*/
	if (recalibrate) 
	{
		recalibrate = 0;
		/**设置磁头执行0柱面*/
		hd_out(dev,hd_info[CURRENT_DEV].sect,0,0,0,WIN_RESTORE,&recal_intr);
		return;
	}	
	/**对于写命令的处理*/
	if (CURRENT->cmd == WRITE) 
	{
		hd_out(dev,nsect,sec,head,cyl,WIN_WRITE,&write_intr);
		for(i=0 ; i<3000 && !(r=inb_p(HD_STATUS)&DRQ_STAT) ; i++)
		{
			/* nothing */ ;
		}
		if (!r) 
		{
			bad_rw_intr();
			goto repeat;
		}
		port_write(HD_DATA,CURRENT->buffer,256);
	} 
	/**对于读命令的处理*/
	else if (CURRENT->cmd == READ) 
	{
		hd_out(dev,nsect,sec,head,cyl,WIN_READ,&read_intr);
	} 
	else
	{
		panic("unknown hd-command");
	}
}
/**硬盘初始化*/
void hd_init(void)
{
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	set_intr_gate(0x2E,&hd_interrupt);
	outb_p(inb_p(0x21)&0xfb,0x21);
	outb(inb_p(0xA1)&0xbf,0xA1);
}
