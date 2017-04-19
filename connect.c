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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if defined(_OPENMP)
	#include <omp.h>
#endif

#include "spinlock.h"

#define MAX_THREADS 1024
#define CACHE_LINE_SIZE 64
#define BUFFER_SIZE 2048

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

#define ASSERT(V) ASSERT_dprintf(V, "bye!\n")

#define ASSERT_dprintf(V, ...) \
	if (unlikely(!(V))) { \
		dprintf("sanity error!\nfile %s at line %u assertion failed!\n%s\n", __FILE__, __LINE__, #V); \
		dprintf(__VA_ARGS__); \
		exit(1); \
	}

#define dprintf(...) printf("task load lib: " __VA_ARGS__)

typedef uint16_t thread_stat_t;

enum {
	THREAD_AVL,
	THREAD_ALIVE,
	THREAD_DEAD
};

static const char *stat_str[] = {
	"avl",
	"alive",
	"dead"
};

typedef struct thread_t {
	pid_t kernel_tid;
	pid_t kernel_pid;
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

static thread_t *alive_threads[MAX_THREADS];
static thread_t *threads_by_order[MAX_THREADS];

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
			dprintf("failed to attach " #FUNC "\n"); \
			if (MUST) \
				exit(1); \
		}\
	}

#define ATTACH_FUNC_VERSION_(FUNC, VERSION, MUST) \
	if (unlikely(REAL_LABEL(FUNC) == NULL)) {\
		REAL_LABEL(FUNC) = dlvsym(RTLD_NEXT, #FUNC, VERSION); \
		if (REAL_LABEL(FUNC) == NULL) {\
			dprintf("failed to attach " #FUNC "\n"); \
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

static uint64_t get_thread_load (pid_t kpid, pid_t ktid)
{
	char buffer[BUFFER_SIZE];
	int fd;
	ssize_t num_read;
	int32_t i;
	char *p;
	uint64_t jiffies_user;

	sprintf(buffer, "/proc/%u/task/%u/stat", (uint32_t)kpid, (uint32_t)ktid);
	fd = open(buffer, O_RDONLY, 0);
	if (fd == -1) {
		dprintf("ERROR! can't open %s\n", buffer);
		return 0;
	}
	
	num_read = read(fd, buffer, BUFFER_SIZE);
	close(fd);
	
	if (num_read == BUFFER_SIZE) {
		dprintf("ERROR! file size is too long to fit in our buffer (size %u)\n", BUFFER_SIZE);
		return 0;
	}

	buffer[num_read] = 0;

	p = strrchr(buffer, ')') + 1;

	for (i=3; i!=14; i++)
		p = strchr(p+1, ' ');

	p++;
/*	printf("buffer: %s\n", buffer);*/
/*	printf("p: %s\n", p);*/
	jiffies_user = strtoull(p, NULL, 10);
	
/*	long jiffies_sys = atol(strchr(ptrUsr,' ') + 1) ;*/

	return jiffies_user;
}

static thread_t* thread_created (uint32_t order)
{
	thread_t *t;
	pid_t ktid, kpid;
	
	kpid = getpid();
	ktid = syscall(__NR_gettid);
	
	spinlock_lock(&threads_lock);

	ASSERT_dprintf(nthreads_total < MAX_THREADS, "Maximum number of threads reached! (%u)\n", MAX_THREADS)
	
	t = &threads[nthreads_total];

	ASSERT(t->stat == THREAD_AVL)
	
	alive_threads[alive_nthreads] = t;
	threads_by_order[order] = t;
	t->alive_pos = alive_nthreads;

	t->stat = THREAD_ALIVE;
	t->pos = nthreads_total;

	t->kernel_pid = kpid;
	t->kernel_tid = ktid;
	t->order_id = order;

	__sync_synchronize();

	alive_nthreads++;
	nthreads_total++;
	
	spinlock_unlock(&threads_lock);
	
	dprintf("thread created %u kpid %u ktid %u\n", t->order_id, (uint32_t)t->kernel_pid, (uint32_t)t->kernel_tid);

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

	dprintf("thread destroyed (order_id=%u old_pos=%u new_pos=%u)\n", t->order_id, old_pos, new_pos);
}

static void* create_head (void *arg)
{
	real_args_t *a = arg;
	void *r;
	
	thread = thread_created(a->order);
	r = a->fn(a->arg);
	thread_destroyed(thread);
	
	return r;
}

int WRAPPER_LABEL(pthread_create) (pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void*), void *arg)
{
	real_args_t *args;
	int pos;
	
/*	if (unlikely(!initialized)) {*/
/*		dprintf("skipping tracking of thread\n");*/
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

	thread = thread_created( get_next_order_id() );

	dprintf("initialized!\n");
}

static void __attribute__((destructor)) triggered_on_app_end ()
{
	uint32_t i;
	thread_t *t;
	uint64_t load;
	
	dprintf("app ended, %u threads were created\n", nthreads_total);
	
	for (i=0; i<nthreads_total; i++) {
		t = threads_by_order[i];
		
		if (likely(t) && t->stat == THREAD_ALIVE) {
			load = get_thread_load(t->kernel_pid, t->kernel_tid);
			dprintf("load of thread (%s) order_id %u is %llu\n", stat_str[t->stat], t->order_id, load);
		}
	}
}

