/*
 * threadpool.hpp
 */

#ifndef THREADPOOL_HPP_
#define THREADPOOL_HPP_

// max number of tasks in queue
#define TASKS_QUEUE_MAX_SIZE 50

// initiate the thread pool
struct threadpool *init_threadpool(int num_threads_in_pool);

// dispatch a new thread
void dispatch(struct threadpool *threadpool, void (*routine)(void*),
	      void *args);

#endif /* THREADPOOL_HPP_ */
