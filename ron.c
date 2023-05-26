#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <immintrin.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>

#include <ron.h>

#include "interpose.h"
#include "utils.h"
#include "waiting_policy.h"

#define core    64
extern __thread unsigned int cur_thread_id;

/* Shared Constants */

static int routing[core] = {0,  1,  2,  3,  32, 33, 34, 35, 4,  5,  6,  7,  36,
                          37, 38, 39, 8,  9,  10, 11, 40, 41, 42, 43, 12, 13,
                          14, 15, 44, 45, 46, 47, 24, 25, 26, 27, 56, 57, 58,
                          59, 28, 29, 30, 31, 60, 61, 62, 63, 16, 17, 18, 19,
                          48, 49, 50, 51, 20, 21, 22, 23, 52, 53, 54, 55};

static int cpu_order[core];

static int cpu_num;

/* Thread Local Variables */

static __thread int cpu;

static __thread int order;
static __thread int idxary[core];

void __ron_set_cpu() {
    cpu = sched_getcpu();
}

/* Lock Implementation */

ron_mutex_t *ron_mutex_create(const pthread_mutexattr_t *attr) {
    cpu_num = get_nprocs();
    for (int cores = 0; cores < cpu_num; cores++)
        cpu_order[routing[cores]] = cores;

    ron_mutex_t *impl = (ron_mutex_t *)alloc_cache_align(sizeof(ron_mutex_t));
    atomic_store_explicit(&impl->locked, 0, memory_order_release);
    memset((void *)impl->wait_array, 0, 128 * sizeof(union extended_atomic));

#if COND_VAR
    REAL(pthread_mutex_init)(&impl->posix_lock, /*&errattr */ attr);
    DEBUG("Mutex init lock=%p posix_lock=%p\n", impl, &impl->posix_lock);
#endif

    return impl;
}

int ron_mutex_lock(ron_mutex_t *impl, ron_context_t *me) {
    int zero;
    atomic_store_explicit( &(impl->wait_array[order].wait), 1, memory_order_release);
    while (1) {
	    //test test_n_set avoid bus traffic
        while (impl->wait_array[order].wait !=0  && impl->locked != 0){
            CPU_PAUSE();
        }
        if(atomic_load_explicit(&(impl->wait_array[order].wait), memory_order_acquire) ==0) {
            goto cond_var;
        }
        zero = 0;
        if (__glibc_unlikely(impl->locked == 0)) {
             if (atomic_compare_exchange_weak_explicit(&(impl->locked), &zero, 1, memory_order_release, memory_order_acquire)) {
                atomic_store_explicit(&(impl->wait_array[order].wait),0, memory_order_release);
                goto cond_var;
            }
        }
    }

cond_var:

#if COND_VAR
    DEBUG_PTHREAD("[%d] Lock posix=%p\n", cur_thread_id, &impl->posix_lock);
    assert(REAL(pthread_mutex_lock)(&impl->posix_lock) == 0);
#endif
    DEBUG("[%d] Lock acquired posix=%p\n", cur_thread_id, &impl->posix_lock);
    return 0;
}

int ron_mutex_trylock(ron_mutex_t *impl, ron_context_t *me) {

    int zero = 0;
    if (atomic_compare_exchange_weak_explicit(&(impl->locked), &zero, 1,
                                              memory_order_release,
                                              memory_order_acquire)) {
#if COND_VAR
        DEBUG_PTHREAD("[%d] Lock posix=%p\n", cur_thread_id, &impl->posix_lock);
        int ret = 0;
        while ((ret = REAL(pthread_mutex_trylock)(&impl->posix_lock)) == EBUSY)
            ;
        assert(ret == 0);
#endif
        return 0;
    }
    return EBUSY;
}

void ron_mutex_unlock(ron_mutex_t *impl, ron_context_t *me) {
#if COND_VAR
    DEBUG_PTHREAD("[%d] Unlock posix=%p\n", cur_thread_id, &impl->posix_lock);
    assert(REAL(pthread_mutex_unlock)(&impl->posix_lock) == 0);
#endif

    // real ron lock1
    for (int i = 1; i < cpu_num; i++) {
        __builtin_prefetch((int*)&impl->wait_array[idxary[i+2]].wait,0,0);
        if ((impl->wait_array[idxary[i]].wait == 1)) {
            atomic_store_explicit(&impl->wait_array[idxary[i]].wait, 0, memory_order_relaxed);
            return;
        }
    }
    atomic_store_explicit(&(impl->locked), 0, memory_order_relaxed);
    return;
}

int ron_mutex_destroy(ron_mutex_t *lock) {
#if COND_VAR
    REAL(pthread_mutex_destroy)(&lock->posix_lock);
#endif
    free(lock);
    lock = NULL;

    return 0;
}

int ron_cond_init(ron_cond_t *cond, const pthread_condattr_t *attr) {
#if COND_VAR
    return REAL(pthread_cond_init)(cond, attr);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int ron_cond_timedwait(ron_cond_t *cond, ron_mutex_t *lock, ron_context_t *me,
                       const struct timespec *ts) {
#if COND_VAR
    // real ron lock
    int res;


    for (int i=1; i < cpu_num; i++) {
        __builtin_prefetch((int*)&lock->wait_array[idxary[i+2]].wait,0,0);
        if (atomic_load_explicit(&lock->wait_array[idxary[i]].wait,memory_order_acquire) == 1) {
            atomic_store_explicit(&lock->wait_array[idxary[i]].wait, 0, memory_order_release);
            goto next;
        }
    }
    atomic_store_explicit(&(lock->locked), 0, memory_order_release);

next:
    DEBUG("[%d] Sleep cond=%p lock=%p posix_lock=%p\n", cur_thread_id, cond,
          lock, &(lock->posix_lock));
    DEBUG_PTHREAD("[%d] Cond posix = %p lock = %p\n", cur_thread_id, cond,
                  &lock->posix_lock);

    if (ts)
        res = REAL(pthread_cond_timedwait)(cond, &lock->posix_lock, ts);
    else
        res = REAL(pthread_cond_wait)(cond, &lock->posix_lock);

    if (res != 0 && res != ETIMEDOUT) {
        fprintf(stderr, "Error on cond_{timed,}wait %d\n", res);
        assert(0);
    }

    int ret = 0;
    if ((ret = REAL(pthread_mutex_unlock)(&lock->posix_lock)) != 0) {
        fprintf(stderr, "Error on mutex_unlock %d\n", ret == EPERM);
        assert(0);
    }

    ron_mutex_lock(lock, me);

    return res;
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int ron_cond_wait(ron_cond_t *cond, ron_mutex_t *lock, ron_context_t *me) {
    return ron_cond_timedwait(cond, lock, me, 0);
}

int ron_cond_signal(ron_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_signal)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int ron_cond_broadcast(ron_cond_t *cond) {
#if COND_VAR
    DEBUG("[%d] Broadcast cond=%p\n", cur_thread_id, cond);
    return REAL(pthread_cond_broadcast)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int ron_cond_destroy(ron_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_destroy)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

atomic_int thread_count = 0;

void ron_thread_start(void) {
    int count = atomic_fetch_add_explicit(&thread_count, 1, memory_order_release)%core;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(count, &set);
    sched_setaffinity(0, sizeof(set), &set);
    cpu = count;
    order = cpu_order[count];
    for (int i=0; i<core; i++)
    	idxary[i]=(order + i)%cpu_num;
}

void ron_thread_exit(void) {
}

void ron_application_init(void) {
}

void ron_application_exit(void) {
}

void ron_init_context(lock_mutex_t *UNUSED(impl),
                      lock_context_t *UNUSED(context), int UNUSED(number)) {
}
