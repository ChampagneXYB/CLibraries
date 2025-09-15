#include "ThreadPool.h"
#include <stdio.h>

void* task(void *arg)
{
    for(int i=0; i<3; i++)
    {
        sleep(1);
        puts("task1 running");
    }
}

int main()
{
    thread_pool* pool = malloc(sizeof(thread_pool));
    init_pool(pool, 5);
    add_task(pool, task, NULL);
    sleep(5);
    return 0;
}