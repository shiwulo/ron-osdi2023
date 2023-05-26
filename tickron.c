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

#include <tickron.h>

#include "interpose.h"
#include "utils.h"
#include "waiting_policy.h"

extern __thread unsigned int cur_thread_id;

/* Shared Constants */

static int routing[64] = {0,  1,  2,  3,  32, 33, 34, 35, 4,  5,  6,  7,  36,
                          37, 38, 39, 8,  9,  10, 11, 40, 41, 42, 43, 12, 13,
                          14, 15, 44, 45, 46, 47, 24, 25, 26, 27, 56, 57, 58,
                          59, 28, 29, 30, 31, 60, 61, 62, 63, 16, 17, 18, 19,
                          48, 49, 50, 51, 20, 21, 22, 23, 52, 53, 54, 55};

static int cpu_order[64];
static int oversub[64];

static int cpu_num;

/* Thread Local Variables */

static __thread int cpu;

static __thread int order;

static __thread unsigned char idxary[67];

void __soa_set_cpu() {
    cpu = sched_getcpu();
}

/* Lock Implementation */

soa_mutex_t *soa_mutex_create(const pthread_mutexattr_t *attr) {
    cpu_num = get_nprocs();
    for (int core = 0; core < cpu_num; core++)
        cpu_order[routing[core]] = core;

    soa_mutex_t *impl = (soa_mutex_t *)alloc_cache_align(sizeof(soa_mutex_t));
    atomic_store_explicit(&impl->wait, 0, memory_order_release);
    for(int i = 0;i < 64;i++){
        impl -> arrayLock[i].grant = 0;
        impl -> arrayLock[i].ticket = 1;
        oversub[i] = 0;
    }


#if COND_VAR
    REAL(pthread_mutex_init)(&impl->posix_lock, /*&errattr */ attr);
    DEBUG("Mutex init lock=%p posix_lock=%p\n", impl, &impl->posix_lock);
#endif

    return impl;
}

int soa_mutex_lock(soa_mutex_t *impl, soa_context_t *me) {
    if(atomic_fetch_add_explicit(&impl -> wait, 1, memory_order_acq_rel) == 0){
        goto cond_var;
    }
    int localTicket = atomic_fetch_add_explicit(&impl -> arrayLock[order].ticket, 1, memory_order_relaxed);
    while(1){
        while(1){
          if((impl -> arrayLock[order].grant) - localTicket != 0 && oversub[order] == 1) 
             sched_yield();
          if(localTicket == (impl -> arrayLock[order].grant))
               break;
          asm("pause");
        }
        if(localTicket == atomic_load_explicit(&impl -> arrayLock[order].grant, memory_order_acquire)){
            goto cond_var;
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

int soa_mutex_trylock(soa_mutex_t *impl, soa_context_t *me) {
    int zero = 0;
    if (atomic_compare_exchange_weak_explicit(&(impl->wait), &zero, 1,
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

void soa_mutex_unlock(soa_mutex_t *impl, soa_context_t *me) {
#if COND_VAR
    DEBUG_PTHREAD("[%d] Unlock posix=%p\n", cur_thread_id, &impl->posix_lock);
    assert(REAL(pthread_mutex_unlock)(&impl->posix_lock) == 0);
#endif
    if(atomic_fetch_sub_explicit(&impl -> wait, 1, memory_order_acq_rel) == 1){
        return;
    }
  
    int next = (order + 1) % 64;

    while(1){
        __builtin_prefetch((int*)&impl -> arrayLock[idxary[next+1]].grant,0,0);
        __builtin_prefetch((int*)&impl -> arrayLock[idxary[next+1]].ticket,0,0);
        if((atomic_load_explicit(&impl -> arrayLock[next].grant, memory_order_acquire) - atomic_load_explicit(&impl -> arrayLock[next].ticket, memory_order_acquire)) <= -2){
            atomic_fetch_add_explicit(&impl -> arrayLock[next].grant, 1, memory_order_acquire);
            return;
        }
        next = (next + 1) % 64;
   }
}

int soa_mutex_destroy(soa_mutex_t *lock) {
#if COND_VAR
    REAL(pthread_mutex_destroy)(&lock->posix_lock);
#endif
    free(lock);
    lock = NULL;

    return 0;
}

int soa_cond_init(soa_cond_t *cond, const pthread_condattr_t *attr) {
#if COND_VAR
    return REAL(pthread_cond_init)(cond, attr);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int soa_cond_timedwait(soa_cond_t *cond, soa_mutex_t *lock, soa_context_t *me,
                       const struct timespec *ts) {

#if COND_VAR
    // real soa lock
    int res;
    if(atomic_fetch_sub_explicit(&lock -> wait, 1, memory_order_acq_rel) == 1){
        goto next;
    }
    int next = (order + 1) % 64;

    while(1){
         __builtin_prefetch((int*)&lock -> arrayLock[idxary[next+1]].grant,0,0);
         __builtin_prefetch((int*)&lock -> arrayLock[idxary[next+1]].ticket,0,0);
        if((atomic_load_explicit(&lock -> arrayLock[next].grant, memory_order_acquire) - atomic_load_explicit(&lock -> arrayLock[next].ticket, memory_order_acquire)) <= -2){
            atomic_fetch_add_explicit(&lock -> arrayLock[next].grant, 1, memory_order_acquire);
            goto next;
        }
        next = (next + 1) % 64;
   }

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

    soa_mutex_lock(lock, me);

    return res;
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif

}

int soa_cond_wait(soa_cond_t *cond, soa_mutex_t *lock, soa_context_t *me) {
    return soa_cond_timedwait(cond, lock, me, 0);
}

int soa_cond_signal(soa_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_signal)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int soa_cond_broadcast(soa_cond_t *cond) {
#if COND_VAR
    DEBUG("[%d] Broadcast cond=%p\n", cur_thread_id, cond);
    return REAL(pthread_cond_broadcast)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

int soa_cond_destroy(soa_cond_t *cond) {
#if COND_VAR
    return REAL(pthread_cond_destroy)(cond);
#else
    fprintf(stderr, "Error cond_var not supported.");
    assert(0);
#endif
}

atomic_int thread_count = 0;

void soa_thread_start(void) {
    int count =
        atomic_fetch_add_explicit(&thread_count, 1, memory_order_release);
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(count % 64, &set);
    sched_setaffinity(0, sizeof(set), &set);
    cpu = count % 64;
    order = cpu_order[count % 64];
    for (int i=0; i<66; i++)
    	idxary[i]=(order + i) % 64;
    if(count > 63)
        oversub[order] = 1;
}

void soa_thread_exit(void) {
}

void soa_application_init(void) {
}

void soa_application_exit(void) {
}

void soa_init_context(lock_mutex_t *UNUSED(impl),
                      lock_context_t *UNUSED(context), int UNUSED(number)) {
}
