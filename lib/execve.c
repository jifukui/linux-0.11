/*
 *  linux/lib/execve.c
 *
 *  (C) 1991  Linus Torvalds
 */

#define __LIBRARY__
#include <unistd.h>


/**使用系统调用3实现execve的功能
 * 
*/
_syscall3(int,execve,const char *,file,char **,argv,char **,envp)
