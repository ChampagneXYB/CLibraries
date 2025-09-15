#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include <pthread.h>

struct task
{
	void *(*do_task)(void *arg); // 函数指针，指向要执行任务
	void *arg;					 // 传递给任务函数的参数
 
	struct task *next;
};

typedef struct thread_pool
{
	pthread_mutex_t lock;  //互斥锁，保护任务队列
	pthread_cond_t cond;   //条件变量，同步所有线程
 
	bool shutdown;   //线程池销毁标记
 
	struct task *task_list;   //任务链队列指针
 
	pthread_t *tids;   //线程ID存放位置
 
	unsigned max_waiting_tasks;   
	unsigned waiting_tasks;   //任务链队列中等待的任务个数
	unsigned active_threads;   //当前活跃线程个数
} thread_pool;

#endif