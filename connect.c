#ifndef _GNU_SOURCE
	#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <dlfcn.h>
#include <unistd.h>
#include <asm/unistd.h>

#if defined(_OPENMP)
	#include <omp.h>
#endif

#include "spinlock.h"

#define MAX_THREADS 1024
#define CACHE_LINE_SIZE 64

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

#define ASSERT(V) ASSERT_PRINTF(V, "bye!\n")

#define ASSERT_PRINTF(V, ...) \
	if (unlikely(!(V))) { \
		printf("sanity error!\nfile %s at line %u assertion failed!\n%s\n", __FILE__, __LINE__, #V); \
		printf(__VA_ARGS__); \
		exit(1); \
	}

typedef uint16_t thread_stat_t;

enum {
	THREAD_AVL,
	THREAD_ALIVE,
	THREAD_DEAD
};

typedef struct thread_t {
	pid_t kernel_tid;
	uint32_t order_id;
	uint32_t pos;
	uint32_t alive_pos;
	thread_stat_t stat;
}  __attribute__ ((aligned(CACHE_LINE_SIZE))) thread_t;

typedef struct real_args_t {
	void *arg;
	void *(*fn)(void*);
	uint32_t order;
} real_args_t;

static thread_t threads[MAX_THREADS] __attribute__ ((aligned(CACHE_LINE_SIZE)));
static uint32_t nthreads_total = 0;
static uint32_t alive_nthreads = 0;
static spinlock_t threads_lock = SPINLOCK_INIT;

thread_t *alive_threads[MAX_THREADS];
thread_t *threads_by_order[MAX_THREADS];

static real_args_t vec[MAX_THREADS];
static int veci = 0;
static uint32_t order = 0;

#define WRAPPER_LABEL(LABEL)                     LABEL
#define REAL_LABEL(LABEL)                        libmapping_connect_real_##LABEL
#define DECLARE_WRAPPER(LABEL, RETTYPE, ...)     static RETTYPE (*REAL_LABEL(LABEL)) (__VA_ARGS__) = NULL

DECLARE_WRAPPER(pthread_create, int, pthread_t*, const pthread_attr_t*, void *(*)(void*), void*);
DECLARE_WRAPPER(pthread_exit, void, void*);

static thread_t dummy_thread = {
	.order_id = 0
};
static __thread thread_t *thread = (thread_t*)&dummy_thread;

#define ATTACH_FUNC_(FUNC, MUST) \
	if (unlikely(REAL_LABEL(FUNC) == NULL)) {\
		REAL_LABEL(FUNC) = dlsym(RTLD_NEXT, #FUNC); \
		if (REAL_LABEL(FUNC) == NULL) {\
			printf("failed to attach " #FUNC "\n"); \
			if (MUST) \
				exit(1); \
		}\
	}

#define ATTACH_FUNC_VERSION_(FUNC, VERSION, MUST) \
	if (unlikely(REAL_LABEL(FUNC) == NULL)) {\
		REAL_LABEL(FUNC) = dlvsym(RTLD_NEXT, #FUNC, VERSION); \
		if (REAL_LABEL(FUNC) == NULL) {\
			printf("failed to attach " #FUNC "\n"); \
			if (MUST) \
				exit(1); \
		}\
	}

#define ATTACH_FUNC(FUNC) ATTACH_FUNC_##FUNC

#define ATTACH_FUNC_pthread_create ATTACH_FUNC_(pthread_create, 1)
#define ATTACH_FUNC_pthread_exit ATTACH_FUNC_(pthread_exit, 1)

static inline uint32_t get_next_order_id ()
{
	uint32_t tmp;
	tmp = __sync_fetch_and_add(&order, 1);
	return tmp;
}

static void init()
{
	uint32_t i;
	
	for (i=0; i<MAX_THREADS; i++) {
		threads[i].stat = THREAD_AVL;
		alive_threads[i] = NULL;
		threads_by_order[i] = NULL;
	}
}

static uint32_t get_current_thread_order_id ()
{
	return thread->order_id;
}

static thread_t* thread_created (uint32_t order, pid_t ktid)
{
	thread_t *t;
	
	spinlock_lock(&threads_lock);

	ASSERT_PRINTF(nthreads_total < MAX_THREADS, "Maximum number of threads reached! (%u)\n", MAX_THREADS)
	
	t = &threads[nthreads_total];

	ASSERT(t->stat == THREAD_AVL)
	
	alive_threads[alive_nthreads] = t;
	threads_by_order[order] = t;
	t->alive_pos = alive_nthreads;

	t->stat = THREAD_ALIVE;
	t->pos = nthreads_total;

	t->kernel_tid = ktid;
	t->order_id = order;

	alive_nthreads++;
	nthreads_total++;
	
	spinlock_unlock(&threads_lock);
	
	printf("thread created %u\n", t->order_id);

	return t;
}

static void thread_destroyed (thread_t *t)
{
	uint32_t old_pos, new_pos;
	
	spinlock_lock(&threads_lock);
	
	alive_nthreads--;
	
	old_pos = alive_nthreads;
	new_pos = t->alive_pos;
	
	ASSERT(new_pos <= old_pos)
	
	alive_threads[old_pos]->alive_pos = new_pos;
	alive_threads[new_pos] = alive_threads[old_pos];
	alive_threads[old_pos] = NULL;
	
	t->stat = THREAD_DEAD;
	
	spinlock_unlock(&threads_lock);

	printf("thread destroyed (order_id=%u old_pos=%u new_pos=%u)\n", t->order_id, old_pos, new_pos);
}

static void* create_head (void *arg)
{
	real_args_t *a = arg;
	void *r;
	
	thread = thread_created(a->order, syscall(__NR_gettid));
	r = a->fn(a->arg);
	thread_destroyed(thread);
	
	return r;
}

int WRAPPER_LABEL(pthread_create) (pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void*), void *arg)
{
	real_args_t *args;
	int pos;
	
/*	if (unlikely(!initialized)) {*/
/*		printf("skipping tracking of thread\n");*/
/*		return REAL_LABEL(pthread_create)(thread, attr, start_routine, arg);*/
/*	}*/

	pos = __sync_fetch_and_add(&veci, 1);

	args = &vec[pos];

	args->arg = arg;
	args->fn = start_routine;
	args->order = get_next_order_id();

	return REAL_LABEL(pthread_create)(thread, attr, create_head, args);
}

void WRAPPER_LABEL(pthread_exit) (void *retval)
{
	if (unlikely(thread == &dummy_thread)) {
		REAL_LABEL(pthread_exit)(retval);
		return;
	}
	
	thread_destroyed(thread);
	REAL_LABEL(pthread_exit)(retval);
}

static thread_t* get_current_thread ()
{
	return thread;
}

static void __attribute__((constructor)) triggered_on_app_start ()
{
	ATTACH_FUNC(pthread_create)
	ATTACH_FUNC(pthread_exit)
	
	ASSERT((sizeof(thread_t) % CACHE_LINE_SIZE) == 0)
	ASSERT(sizeof(unsigned long) == sizeof(void*))

	init();

	thread = thread_created( get_next_order_id(), syscall(__NR_gettid) );

	printf("initialized!\n");
}

static void __attribute__((destructor)) triggered_on_app_end ()
{
	printf("app ended\n");
}

