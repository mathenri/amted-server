/*
 * threadpool.cpp
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "threadpool.hpp"

struct task {
	// the routine this task will call
	void (*routine)(void*);

	// the parameters the routine takes
	void *args;
};

/*
 * A queue of tasks waiting to be executed by a thread in the threadpool
 */
struct tasks_queue {

	// number of elements currently in queue
	int nr_elements;

	int head;
	int tail;

	// an array to queue up the elements in
	struct task *elements[TASKS_QUEUE_MAX_SIZE];
};

/*
 * Struct containing information regarding a specific thread pool
 */
struct threadpool {

	struct tasks_queue tasks_queue;
	int nr_threads;
	pthread_t *threads;
	pthread_mutex_t lock;

	/* condition variable for threads to wait on when there are no tasks on the
	 queue */
	pthread_cond_t queue_not_empty;
};

/*
 * Adds a task to the back of a queue
 */
void queue_enqueue(struct tasks_queue *queue, struct task *task) {
	queue->elements[queue->tail] = task;
	queue->tail++;
	queue->nr_elements++;
	if (queue->tail == TASKS_QUEUE_MAX_SIZE) {
		queue->tail = 0;
	}
}

/*
 * Pulls out a task from the front of a queue
 */
struct task *queue_dequeue(struct tasks_queue *queue) {
	struct task *task = queue->elements[queue->head];

	queue->elements[queue->head] = NULL;
	queue->nr_elements--;

	queue->head++;
	if (queue->head == TASKS_QUEUE_MAX_SIZE) {
		queue->head = 0;
	}

	return task;
}

/*
 * Takes a task off the task queue and calls read_file with this task's
 * parameters. If there are no tasks on the queue, the thread goes to sleep on
 * the queue-empty condition variable.
 */
void *work(void *tp) {
	struct threadpool *threadpool = (struct threadpool *) tp;
	struct task *task;

	while (1) {

		// acquire the lock protecting the queue of tasks
		pthread_mutex_lock(&(threadpool->lock));

		// while no tasks on the tasks_queue ...
		while (threadpool->tasks_queue.nr_elements == 0) {

			// ... wait on condition variable till there are new tasks
			pthread_cond_wait(&(threadpool->queue_not_empty),
					&(threadpool->lock));
		}

		// grab a task from the task queue
		task = queue_dequeue(&(threadpool->tasks_queue));
		pthread_mutex_unlock(&(threadpool->lock));

		// execute routine
		task->routine(task->args);
		free(task);
	}
	return NULL;
}

void dispatch(struct threadpool *threadpool, void (*routine)(void*),
		void *args) {
	struct task *task;

	// create a new task object
	task = (struct task *) malloc(sizeof(task));
	if (task == NULL) {
		perror("malloc");
		return;
	}

	task->routine = routine;
	task->args = args;

	// add new task to tasks queue
	pthread_mutex_lock(&(threadpool->lock));
	queue_enqueue(&(threadpool->tasks_queue), task);
	pthread_mutex_unlock(&(threadpool->lock));

	// notify sleeping threads that there are new tasks to work on
	pthread_cond_signal(&(threadpool->queue_not_empty));
}

/*
 * Setup a new thread pool with a given number of threads. Returns NULL if setup
 * fails.
 */
struct threadpool *init_threadpool(int nr_threads) {
	struct threadpool *threadpool;

	// allocate memory for the thread pool
	threadpool = (struct threadpool*) malloc(sizeof(struct threadpool));
	if (threadpool == NULL) {
		perror("malloc");
		return NULL;
	}

	//initialize lock and condition variables.
	if (pthread_mutex_init(&threadpool->lock, NULL)) {
		perror("pthread_mutex_init");
		return NULL;
	}
	if (pthread_cond_init(&(threadpool->queue_not_empty), NULL)) {
		perror("pthread_cond_init");
		return NULL;
	}

	// init tasks tasks_queue
	int i;
	for (i = 0; i < TASKS_QUEUE_MAX_SIZE; i++) {
		threadpool->tasks_queue.elements[i] = NULL;
	}
	threadpool->tasks_queue.head = 0;
	threadpool->tasks_queue.tail = 0;
	threadpool->tasks_queue.nr_elements = 0;

	// allocate threads array
	threadpool->nr_threads = nr_threads;
	threadpool->threads = (pthread_t*) malloc(sizeof(pthread_t) * nr_threads);
	if (!threadpool->threads) {
		perror("malloc");
		return NULL;
	}

	// initialize threads
	int j;
	for (j = 0; j < nr_threads; j++) {
		if (pthread_create(&(threadpool->threads[j]), NULL, work, threadpool)) {
			perror("pthreads_create");
			return NULL;
		}
	}
	return threadpool;
}

