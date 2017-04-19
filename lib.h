#ifndef __LIBTLOAD_H__
#define __LIBTLOAD_H__

#include <stdint.h>
#include <sys/types.h>

#define MAX_THREADS 1024
#define CACHE_LINE_SIZE 64

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

#define ASSERT(V) ASSERT_PRINTF(V, "bye!\n")

#define ASSERT_PRINTF(V, ...) \
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

typedef struct thread_t {
	pid_t kernel_tid;
	pid_t kernel_pid;
	uint32_t order_id;
	uint32_t pos;
	uint32_t alive_pos;
	thread_stat_t stat;
}  __attribute__ ((aligned(CACHE_LINE_SIZE))) thread_t;

static inline uint32_t libtload_get_total_nthreads ()
{
	extern uint32_t libtload_nthreads_total;
	return libtload_nthreads_total;
}

thread_t* libtload_get_current_thread ();
uint32_t libtload_get_current_thread_order_id ();

char* libtload_env_get_str(char *envname);
char* libtload_strtok (char *str, char *tok, char del, uint32_t bsize);

#endif
