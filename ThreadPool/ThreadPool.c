#include "ThreadPool.h"

void handler(void *arg)
{
	printf("[%u] is ended.\n",
		   (unsigned)pthread_self());
 
	pthread_mutex_unlock((pthread_mutex_t *)arg);
}

// 线程的任务函数 ，所有重点操作都在该函数中
void *routine(void *arg)
{
 
#ifdef DEBUG
	printf("[%u] is started.\n", (unsigned)pthread_self()); // 打印线程的TID
#endif
 
	// 获取线程池结构体的地址,因为条件变量和互斥锁都在该结构体定义
	thread_pool *pool = (thread_pool *)arg;
	// 定义任务指针,指向要提取的任务
	struct task *p;
 
	while (1)
	{
		/*
		** push a cleanup functon handler(), make sure that
		** the calling thread will release the mutex properly
		** even if it is cancelled during holding the mutex.
		**
		** NOTE:
		** pthread_cleanup_push() is a macro which includes a
		** loop in it, so if the specified field of codes that
		** paired within pthread_cleanup_push() and pthread_
		** cleanup_pop() use 'break' may NOT break out of the
		** truely loop but break out of these two macros.
		** see line 61 below.
		*/
		//================================================//
		// 注册取消处理函数,&pool->lock 把锁传递过去，当前线程突然被取消时，防止死锁
		pthread_cleanup_push(handler, (void *)&pool->lock);
 
		// 上锁 ,因为所有线程都是在同一个任务列表中提取任务,用互斥锁保护链表中的任务
		pthread_mutex_lock(&pool->lock);
		//================================================//
 
		// 1, no task, and is NOT shutting down, then wait
		while (pool->waiting_tasks == 0 && !pool->shutdown)
		{
			pthread_cond_wait(&pool->cond, &pool->lock); // 如果没有任何任务，则进入等待状态
		}
 
		// 2, no task, and is shutting down, then exit
		if (pool->waiting_tasks == 0 && pool->shutdown == true)
		{
			pthread_mutex_unlock(&pool->lock);
			pthread_exit(NULL); // CANNOT use 'break'; //如果没有任务且是关机状态则，退出线程
		}
 
		// 3, have some task, then consume it 如果有任务
		p = pool->task_list->next;		 // 指向第一个任务节点
		pool->task_list->next = p->next; // 把任务节点从链表提取出来
		pool->waiting_tasks--;			 // 任务数量减少
 
		//================================================//
		pthread_mutex_unlock(&pool->lock); // 解锁 ，因为已经取完了任务
		pthread_cleanup_pop(0);
		//================================================//
 
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); // 关闭取消请求,确保任务被执行完毕
		(p->do_task)(p->arg);								  // 执行任务并传递参数 void *(*do_task)(void *arg);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);  // 任务执行完毕后重启开启取消请求
 
		free(p); // 释放任务节点
	}
 
	pthread_exit(NULL);
}

//初始化线程池结构体 
bool init_pool(thread_pool *pool, unsigned int threads_number)
{
    //初始化锁和条件变量  
    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->cond, NULL);
 
    pool->shutdown = false; //关键标记设置为假 
    pool->task_list = malloc(sizeof(struct task)); //初始化头节点  
    pool->tids = malloc(sizeof(pthread_t) * MAX_ACTIVE_THREADS); //sizeof(pthread_t) * 20 ,数组有20个元素        
    
    //判断堆空间是否分配成功
    if(pool->task_list == NULL || pool->tids == NULL)
    {
        perror("allocate memory error");
        return false;
    }
    //初始化任务链表头节点
    pool->task_list->next = NULL;
    //初始化最大任务数 
    pool->max_waiting_tasks = MAX_WAITING_TASKS; //最多可以有 XX 个任务  
    pool->waiting_tasks = 0; //当前等待的任务数为 0  
    pool->active_threads = threads_number; //需要创建的活跃线程数 
 
    int i;
    for(i=0; i<pool->active_threads; i++)  //循环创建多个线程
    {  
       //&((pool->tids)[i] 把tid存储到堆空间 ,routine 线程执行的工作,把整个管理结构体传递给线程 
        if(pthread_create(&((pool->tids)[i]), NULL,routine, (void *)pool) != 0)
        {
            perror("create threads error");
            return false;
        }
        #ifdef DEBUG
        printf("[%u]:[%s] ==> tids[%d]: [%u] is created.\n",
            (unsigned)pthread_self(), __FUNCTION__,
            i, (unsigned)pool->tids[i]);
        #endif
    }
    return true;
}

bool add_task(thread_pool *pool, void *(*do_task)(void *arg), void *arg)
{
	// 新建一个任务节点
	struct task *new_task = malloc(sizeof(struct task));
	if (new_task == NULL)
	{
		perror("allocate memory error");
		return false;
	}
	// 初始化任务节点
	new_task->do_task = do_task;
	new_task->arg = arg;
	new_task->next = NULL;
 
	// 上锁 ,防止在添加任务的时候，有别的线程去拿任务
	//============ LOCK =============//
	pthread_mutex_lock(&pool->lock);
	//===============================//
 
	// 如果当前等待的任务 大于 最大任务数
	if (pool->waiting_tasks >= MAX_WAITING_TASKS)
	{
		pthread_mutex_unlock(&pool->lock); // 解锁
		fprintf(stderr, "too many tasks.\n");
		free(new_task); // 放弃任务
		return false;	// 结束函数
	}
 
	struct task *tmp = pool->task_list;
	while (tmp->next != NULL)
		tmp = tmp->next; // 偏移链表末尾
 
	tmp->next = new_task;  // 尾插
	pool->waiting_tasks++; // 任务数量增加
 
	//=========== UNLOCK ============//
	pthread_mutex_unlock(&pool->lock); // 任务已经添加完毕了，可以解锁了
									   //===============================//
 
#ifdef DEBUG
	printf("[%u][%s] ==> a new task has been added.\n",
		   (unsigned)pthread_self(), __FUNCTION__);
#endif
 
	pthread_cond_signal(&pool->cond); // 通知正在休眠的线程干活！
	return true;
}

int add_thread(thread_pool *pool, unsigned additional_threads)
{
	// 如果添加的线程数为 0 则 退出
	if (additional_threads == 0)
		return 0;
 
	// 总线程数                 当前的              +  添加的
	unsigned total_threads = pool->active_threads + additional_threads;
 
	int i, actual_increment = 0;
	// 循环创建新的任务线程
	for (i = pool->active_threads; i < total_threads && i < MAX_ACTIVE_THREADS; i++)
	{
		if (pthread_create(&((pool->tids)[i]), NULL, routine, (void *)pool) != 0)
		{
			perror("add threads error");
 
			// no threads has been created, return fail
			if (actual_increment == 0)
				return -1;
 
			break;
		}
		// 增加创建成功的线程数
		actual_increment++;
 
#ifdef DEBUG
		printf("[%u]:[%s] ==> tids[%d]: [%u] is created.\n",
			   (unsigned)pthread_self(), __FUNCTION__,
			   i, (unsigned)pool->tids[i]);
#endif
	}
	pool->active_threads += actual_increment;
	// 返回成功创建的线程数
	return actual_increment;
}

int remove_thread(thread_pool *pool, unsigned int removing_threads)
{
	// 如果需要删除 0 个直接退出
	if (removing_threads == 0)
		return pool->active_threads;
 
	//              剩余      = 当前的  -  需要删除的
	int remaining_threads = pool->active_threads - removing_threads;
 
	// 如果剩余的 小于 0 ,则保留一个线程,执行任务
	remaining_threads = remaining_threads > 0 ? remaining_threads : 1;
 
	int i;
	for (i = pool->active_threads - 1; i > remaining_threads - 1; i--)
	{
		// 循环取消线程
		errno = pthread_cancel(pool->tids[i]);
		if (errno != 0)
			break;
#ifdef DEBUG
		printf("[%u]:[%s] ==> cancelling tids[%d]: [%u]...\n",
			   (unsigned)pthread_self(), __FUNCTION__,
			   i, (unsigned)pool->tids[i]);
#endif
	}
 
	// 返回剩下的活跃线程
	if (i == pool->active_threads - 1)
		return -1;
	else
	{
		pool->active_threads = i + 1;
		return i + 1;
	}
}

bool destroy_pool(thread_pool *pool)
{
	// 1, activate all threads
	pool->shutdown = true; // 设置关机标记为真
 
	// 广播条件变量，唤醒所有休眠的线程
	pthread_cond_broadcast(&pool->cond);
 
	// 2, wait for their exiting
	int i;
	for (i = 0; i < pool->active_threads; i++)
	{
		errno = pthread_join(pool->tids[i], NULL); // 循环回收线程资源
		if (errno != 0)
		{
			printf("join tids[%d] error: %s\n",
				   i, strerror(errno));
		}
		else
			printf("[%u] is joined\n", (unsigned)pool->tids[i]);
	}
 
	// 3, free memories 释放所有的堆空间
	free(pool->task_list);
	free(pool->tids);
	free(pool);
 
	return true;
}