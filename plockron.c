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

#include <plockron.h>

#include "interpose.h"
#include "utils.h"
#include "waiting_policy.h"

#define core    64
extern __thread unsigned int cur_thread_id;

/* Shared Constants */
static int start_number[64] = { 61,15,56,43,45,29,36,27,42,23,8,9,54,47,40,19,38,6,17,60,55,46,35,49,52,44,57,28,39,4,48,0,3,7,37,63,34,58,16,12,10,31,30,24,50,1,41,20,13,22,51,18,5,11,14,62,59,26,2,21,25,53,32};

static int routing[core] = {0,  1,  2,  3,  32, 33, 34, 35, 4,  5,  6,  7,  36,
                          37, 38, 39, 8,  9,  10, 11, 40, 41, 42, 43, 12, 13,
                          14, 15, 44, 45, 46, 47, 24, 25, 26, 27, 56, 57, 58,
                          59, 28, 29, 30, 31, 60, 61, 62, 63, 16, 17, 18, 19,
                          48, 49, 50, 51, 20, 21, 22, 23, 52, 53, 54, 55};


/*
static int routing[core] = {  0,  27,  26,  25, 24, 91, 90, 89,  88,  87,  86,  85,  84,  23,  22,  21,  20,  19,  18,  17, 16,
                83,  82,  81,  80, 95, 94, 93, 92,  31,  30,  29,  28,  51,  50,  49,  48, 115, 114, 113, 112,
                127, 126, 125, 124, 63, 62, 61, 60, 119, 118, 117, 116,  55,  54,  53,  52, 123, 122, 121, 120,
                59,  58,  57,  56, 35, 34, 33, 32,  99,  98,  97,  96, 111, 110, 109, 108,  47,  46,  45,  44,
                103, 102, 101, 100, 39, 38, 37, 36, 107, 106, 105, 104,  43,  42,  41,  40,  79,  78,  77,  76,
                15,  14,  13,  12, 75, 74, 73, 72,  11,  10,   9,   8,  71,  70,  69,  68,   7,   6,   5,   4,
                67,  66,  65,  64,  3,  2,  1};
*/
static int cpu_order[core];

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
    for (int cores = 0; cores < cpu_num; cores++)
        cpu_order[routing[cores]] = cores;

    soa_mutex_t *impl = (soa_mutex_t *)alloc_cache_align(sizeof(soa_mutex_t));

    atomic_store_explicit(&impl -> inUse, 0, memory_order_release);
    for(int i = 0;i < core;i++){
        impl -> arrayLock[i].numWait = 0;
        impl -> arrayLock[i].lock = 1;
    }
#if COND_VAR
    REAL(pthread_mutex_init)(&impl->posix_lock, /*&errattr */ attr);
    DEBUG("Mutex init lock=%p posix_lock=%p\n", impl, &impl->posix_lock);
#endif
    return impl;
}

int soa_mutex_lock(soa_mutex_t *impl, soa_context_t *me) {
    //printf("lock %d\n",order);
    atomic_fetch_add_explicit(&impl -> arrayLock[order].numWait, 1, memory_order_acq_rel);
    int zero = 0;
    atomic_bool boolZero = 0;
    while(1){
        while(impl -> arrayLock[order].lock != 0 && impl -> inUse != 0)
            asm("pause");    //**while中判斷lock==1&&inUse==1改成lock==1&&inUse!=0
        
        if(atomic_compare_exchange_strong_explicit(&impl -> arrayLock[order].lock, &zero, 1, memory_order_acq_rel, memory_order_relaxed))
        {
            goto cond_var;
        }
        
        if(__glibc_unlikely(atomic_compare_exchange_strong_explicit(&impl -> inUse, &boolZero, 1, memory_order_acq_rel, memory_order_relaxed)))
            goto cond_var;
        zero = 0;
        boolZero = 0;
    }

cond_var:

#if COND_VAR
    DEBUG_PTHREAD("[%d] Lock posix=%p\n", cur_thread_id, &impl->posix_lock);
    assert(REAL(pthread_mutex_lock)(&impl->posix_lock) == 0);
#endif
    DEBUG("[%d] Lock acquired posix=%p\n", cur_thread_id, &impl->posix_lock);
    //printf("IN %d\n",order);
    return 0;
}

int soa_mutex_trylock(soa_mutex_t *impl, soa_context_t *me) {
    atomic_bool zero = 0;
    if (atomic_compare_exchange_weak_explicit(&(impl->inUse), &zero, 1,
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
    //printf("dont want %d\n",order);
#if COND_VAR
    DEBUG_PTHREAD("[%d] Unlock posix=%p\n", cur_thread_id, &impl->posix_lock);
    assert(REAL(pthread_mutex_unlock)(&impl->posix_lock) == 0);
#endif
    //atomic_fetch_sub_explicit(&impl -> arrayLock[order].numWait, -1, memory_order_acq_rel);
    for(int i = 1;i < core;i++){
        //__builtin_prefetch((int*)&impl -> arrayLock[idxary[i+2]].lock,0,0);
        __builtin_prefetch((int *)&impl -> arrayLock[idxary[i + 2]].numWait, 0, 0);
        if (atomic_load_explicit(&impl -> arrayLock[idxary[i]].numWait, memory_order_acquire) > 0)
        {
            //if (atomic_load_explicit(&impl -> arrayLock[idxary[i]].lock, memory_order_acquire) == 1)
            {
                atomic_store_explicit(&impl -> arrayLock[idxary[i]].lock, 0, memory_order_release);
                atomic_fetch_add_explicit(&impl -> arrayLock[order].numWait, -1, memory_order_acq_rel);
                return;
            }
        }
   }
   atomic_store_explicit(&impl -> inUse, 0, memory_order_release);
   atomic_fetch_add_explicit(&impl -> arrayLock[order].numWait, -1, memory_order_acq_rel);
   //printf("%d free lock\n",order);
   return;
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
    atomic_fetch_sub_explicit(&lock -> arrayLock[order].numWait, 1, memory_order_acq_rel);
/*
    int next = (order + 1) % core;
    for(int i = 1;i < 1024;i++){
        if(atomic_load_explicit(&lock -> arrayLock[next].numWait, memory_order_acquire) > 0){
            if (atomic_load_explicit(&lock -> arrayLock[next].lock, memory_order_acquire) == 1){
                atomic_store_explicit(&lock -> arrayLock[next].lock, 0, memory_order_release);
                goto next1;
            }
        }
        next = (next + 1) % core;
    }
*/

    for(int i = 1;i < core;i++){
        __builtin_prefetch((int*)&lock -> arrayLock[idxary[i+2]].lock,0,0);
        __builtin_prefetch((int *)&lock -> arrayLock[idxary[i + 2]].numWait, 0, 0);
        if (atomic_load_explicit(&lock -> arrayLock[idxary[i]].numWait, memory_order_acquire) > 0)
        {
            if (atomic_load_explicit(&lock -> arrayLock[idxary[i]].lock, memory_order_acquire) == 1)
            {
           //     atomic_fetch_add_explicit(&lock -> arrayLock[idxary[core]].numWait, -1, memory_order_acq_rel);
                    atomic_store_explicit(&lock -> arrayLock[idxary[i]].lock, 0, memory_order_release);
                    goto next1;
            }
        }
    } 
  //  atomic_fetch_add_explicit(&lock -> arrayLock[order].numWait, -1, memory_order_acq_rel);
    atomic_store_explicit(&lock -> inUse, 0, memory_order_release);

next1:
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
int Rrand_Array[core] = {0};
atomic_int lin_round = 1;



void soa_thread_start(void) {
    //int count = rand_array() ;
    
    int count = start_number[(atomic_fetch_add_explicit(&thread_count, 1, memory_order_release))%core];
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(count, &set);
    sched_setaffinity(0, sizeof(set), &set);
    cpu = count;
    order = cpu_order[count];
    for (int i=0; i<core; i++)
    	idxary[i]=(order + i) % core;
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
