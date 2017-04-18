#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

typedef int spinlock_t;

#if defined(__x86_64__)
	#define CPU_PAUSE __asm__ __volatile__ ("pause");
#else
	#define CPU_PAUSE
#endif

#define SPINLOCK_INIT 0

static inline void spinlock_lock(spinlock_t *exclusion)
{
	while (__sync_lock_test_and_set(exclusion, 1)) {
		CPU_PAUSE
	}
}

static inline void spinlock_unlock(spinlock_t *exclusion)
{
	__sync_synchronize(); // Memory barrier.
	__sync_lock_release(exclusion);
}

#endif
