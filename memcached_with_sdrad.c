/**
 * @file memcached_with_sdrad.c
 * @author Merve Gülmez 
 * @brief 
 * @version 0.1
 * @date 2022-02-07
 * 
 * @copyright © Ericsson AB 2022-2023
 * 
 * SPDX-License-Identifier: BSD 3-Clause
 */

#include "config.h"
#include "memcached.h"
#include "cache.h"
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <sys/resource.h> 
#include "../secure-rewind-and-discard/src/sdrad_api.h"
#include "memcached_with_sdrad.h"
#include  <stdbool.h>
#include "bipbuffer.h"



__thread unsigned long tid = 0;
__thread int m_initilazation = false; 
__thread conn *c_copy;
__thread LIBEVENT_THREAD *thread_copy;
__thread logger *domain_log; 
__thread void *m_cache; 
__thread int m_nested_domain_call; 
__thread struct ms_database_integrity_s  ms_data __attribute__((aligned(0x1000))); 
__thread struct ms_database_integrity_s *ms_data_ptr;


typedef  uint32_t  ub4;
#define hashsize(n) ((ub4)1<<(n))
#define hashmask(n) (hashsize(n)-1)

pthread_mutex_t *shared_item_locks;
pthread_mutex_t *shared_try_item_locks;
/*wrapper version of memchached data storage functions*/
void  __wrap_slabs_init(const size_t limit, const double factor, const bool prealloc, const uint32_t *slab_sizes, void *mem_base_external, bool reuse_mem); 
void* __wrap_do_cache_alloc(cache_t *cache);
void  __wrap_do_cache_free(cache_t *cache, void *ptr);
void* __wrap_slabs_alloc(size_t size, unsigned int id, unsigned int flags);
void  __wrap_item_remove(item *item);
void  __wrap_do_item_remove(item *it);
void  __wrap_assoc_init(const int hashtable_init);
int   __wrap_assoc_insert(item *it, const uint32_t hv);
item* __wrap_item_get(const char *key, const size_t nkey, conn *c, const bool do_update);
item* __wrap_assoc_find(const char *key, const size_t nkey, const uint32_t hv);
enum store_item_type __wrap_store_item(item *item, int comm, conn* c);

/*wrapper version of memchached functions*/
void* __real_do_cache_alloc(cache_t *cache);
void* __wrap_do_cache_alloc(cache_t *cache)
{
    return __real_do_cache_alloc(m_cache);  
}

void __real_do_cache_free(cache_t *cache, void *ptr);
void __wrap_do_cache_free(cache_t *cache, void *ptr) 
{
    __real_do_cache_free(m_cache, ptr);
}

cache_t* __cache_create(unsigned long udi, const char *name, size_t bufsize, size_t align);
cache_t* __cache_create(unsigned long udi, const char *name, size_t bufsize, size_t align) 
{
    cache_t* ret = sdrad_calloc(udi, 1, sizeof(cache_t));
    char* nm = strdup(name);
    if (ret == NULL || nm == NULL ||
        pthread_mutex_init(&ret->mutex, NULL) == -1) {
        free(ret);
        free(nm);
        return NULL;
    }

    ret->name = nm;
    STAILQ_INIT(&ret->head);

#ifndef NDEBUG
    ret->bufsize = bufsize + 2 * sizeof("deadbeefcafedeed");
#else
    ret->bufsize = bufsize;
#endif
    assert(ret->bufsize >= sizeof(struct cache_free_s));
    return ret;
}

void *m_cache_create(unsigned long udi)
{
    return __cache_create(udi, "rbuf", READ_BUFFER_SIZE, sizeof(char *));
}

void __real_slabs_init(const size_t limit, const double factor, const bool prealloc, const uint32_t *slab_sizes, void *mem_base_external, bool reuse_mem); 
void __wrap_slabs_init(const size_t limit, const double factor, const bool prealloc, const uint32_t *slab_sizes, void *mem_base_external, bool reuse_mem)
{
    sdrad_init(MEMCACHED_DATA_UDI, SDRAD_DATA_DOMAIN); 
    sdrad_dprotect(0, MEMCACHED_DATA_UDI, 0);
    __real_slabs_init(limit, factor,  prealloc, slab_sizes, mem_base_external, reuse_mem); 
}



void *__real_slabs_alloc(size_t size, unsigned int id,
                        unsigned int flags);
void *__wrap_slabs_alloc(size_t size, unsigned int id,
                        unsigned int flags) 
{
    void   *real_it;

    if(m_nested_domain_call == 0){
        real_it = __real_slabs_alloc(size,  id, flags);
        return real_it;
    }else{
        ms_data_ptr -> m_item_size = size; 
        ms_data_ptr -> m_slabs_id = id; 
        ms_data_ptr -> sdrad_slabs_alloc_flag = 1; 
        if(ms_data_ptr -> slabs_alloc_flag == 0){
            ms_data_ptr -> it_copy = sdrad_malloc(pthread_self(), 40960);
            ms_data_ptr -> slabs_alloc_flag = 1;             
        }
        ms_data_ptr -> it_copy->it_flags &= ~ITEM_SLABBED;
        ms_data_ptr -> it_copy->refcount = 1;
    }
    return ms_data_ptr -> it_copy; 
}

void __real_do_item_remove(item *it);
void __wrap_do_item_remove(item *it) {
    if(m_nested_domain_call == 1)
        ms_data_ptr -> m_deleted_flag = 1; 
    else 
        __real_do_item_remove(it);
}

void __real_item_remove(item *item);
void __wrap_item_remove(item *item) 
{
    uint32_t hv;
    hv = hash(ITEM_key(item), item->nkey);
    __wrap_item_lock(hv);
    __wrap_do_item_remove(item);   // attention it is not __wrap_item_remove
    __wrap_item_unlock(hv);
}

void __real_assoc_init(const int hashtable_init);
void __wrap_assoc_init(const int hashtable_init) 
{
    int i;
    uint32_t item_lock_count;
    int         power;

    sdrad_init(MEMCACHED_DATA_UDI, SDRAD_DATA_DOMAIN ); 
    sdrad_init(MEMCACHED_HT_MUTEX_UDI, SDRAD_DATA_DOMAIN ); 
    sdrad_dprotect(0, MEMCACHED_DATA_UDI, 0);
    sdrad_dprotect(0, MEMCACHED_HT_MUTEX_UDI, 0);
    __real_assoc_init(hashtable_init); 

    if (settings.num_threads < 3) {
        power = 10;
    } else if (settings.num_threads < 4) {
        power = 11;
    } else if (settings.num_threads < 5) {
        power = 12;
    } else if (settings.num_threads <= 10) {
        power = 13;
    } else if (settings.num_threads <= 20) {
        power = 14;
    } else {
        /* 32k buckets. just under the hashpower default. */
        power = 15;
    }

    item_lock_count = hashsize(power);
    shared_item_locks = sdrad_calloc(MEMCACHED_HT_MUTEX_UDI, item_lock_count, sizeof(pthread_mutex_t)); //todo
    for (i = 0; i < item_lock_count; i++) {
        pthread_mutex_init(&shared_item_locks[i], NULL);
    }
}


void __real_item_lock(uint32_t hv);
void __wrap_item_lock(uint32_t hv) {
    mutex_lock(&shared_item_locks[hv & hashmask(item_lock_hashpower)]);
}

void __real_item_unlock(uint32_t hv);
void __wrap_item_unlock(uint32_t hv) {
    mutex_unlock(&shared_item_locks[hv & hashmask(item_lock_hashpower)]);
}


void __wrap_item_trylock_unlock(void *lock);
void __real_item_trylock_unlock(void *lock);
void __wrap_item_trylock_unlock(void *lock) {
    mutex_unlock((pthread_mutex_t *) lock);
}


void *__wrap_item_trylock(uint32_t hv);
void *__real_item_trylock(uint32_t hv); 
void *__wrap_item_trylock(uint32_t hv) {
    pthread_mutex_t *lock = &shared_item_locks[hv & hashmask(item_lock_hashpower)];
    if (pthread_mutex_trylock(lock) == 0) {
        return lock;
    }
    return NULL;
}

item *__real_item_get(const char *key, const size_t nkey, conn *c, const bool do_update); 
item *__wrap_item_get(const char *key, const size_t nkey, conn *c, const bool do_update) 
{
    item *it;
    uint32_t hv; 
    hv = hash(key, nkey);
    __wrap_item_lock(hv);
    it = do_item_get(key, nkey, hv, c, false);
    if (it != NULL){
        ms_data_ptr ->m_get_command = 1; 
        if(do_update == true)
            ms_data_ptr ->m_item_bump_flag = true;
    }
    __wrap_item_unlock(hv);
    return it; 
}

int __wrap_item_link(item *item);
int __real_item_link(item *item);
int __wrap_item_link(item *item) 
{
    int ret;
    uint32_t hv;

    hv = hash(ITEM_key(item), item->nkey);
    __wrap_item_lock(hv);
    ret = do_item_link(item, hv);
    __wrap_item_unlock(hv);
    return ret;
}

void __wrap_item_unlink(item *item);
void __real_item_unlink(item *item);
void __wrap_item_unlink(item *item) {
    uint32_t hv;
    hv = hash(ITEM_key(item), item->nkey);
    __wrap_item_lock(hv);
    do_item_unlink(item, hv);
    __wrap_item_unlock(hv);
}

item *__real_assoc_find(const char *key, const size_t nkey, const uint32_t hv); 
item *__wrap_assoc_find(const char *key, const size_t nkey, const uint32_t hv)
{
    if(ms_data_ptr ->m_assoc_trusted_flag == 0)
        return __real_assoc_find(key,  nkey,  hv); 
    item *it = __real_assoc_find(key,  nkey,  hv);  

    if (it == NULL){
        return NULL;
    }else{
        if(ms_data_ptr ->m_assoc_find_it_flag == 0) {
            ms_data_ptr ->m_assoc_find_it = sdrad_malloc(pthread_self(), 4096);
            ms_data_ptr ->m_assoc_find_it_flag = 1;
        }
        ms_data_ptr ->m_get_it = it; 
        ms_data_ptr ->m_assoc_find_event_flag = 1; 
        memcpy(ms_data_ptr ->m_assoc_find_it, it, sizeof(item));
        memcpy(ITEM_data(ms_data_ptr ->m_assoc_find_it), ITEM_data(it), it->nbytes);
        memcpy(ITEM_key(ms_data_ptr ->m_assoc_find_it), ITEM_key(it), it->nkey);
    } 
    return ms_data_ptr ->m_assoc_find_it;
}

/*  wrap_item_lock is needed or not */
enum store_item_type __real_store_item(item *item, int comm, conn* c);
enum store_item_type __wrap_store_item(item *item, int comm, conn* c) 
{
    if(ms_data_ptr -> m_store_find_init_flag == 0) {
        ms_data_ptr -> m_store_it_copy = sdrad_malloc(pthread_self(), 4096);
        ms_data_ptr -> m_store_find_init_flag = 1;
    } 
    memcpy(ITEM_data(ms_data_ptr ->m_store_it_copy), ITEM_data(item), item->nbytes);
    ms_data_ptr ->m_store_comm = comm; 
    ms_data_ptr ->m_store_it_flag = 1;
    return 1;   ///TODO
}

static size_t bipbuf_sizeof(const unsigned int size)
{
    return sizeof(bipbuf_t) + size;
}

bipbuf_t *sdrad_bipbuf_new(unsigned long tid, const unsigned int size);
bipbuf_t *sdrad_bipbuf_new(unsigned long tid, const unsigned int size)
{
    bipbuf_t *me = sdrad_malloc(tid, bipbuf_sizeof(size));
    if (!me)
        return NULL;
    bipbuf_init(me, size);
    return me;
}


logger *domain_logger_create(unsigned long tid) 
{
    logger *l = sdrad_calloc(tid, 1, sizeof(logger));
    if (l == NULL) {
        return NULL;
    }
    l->buf = sdrad_bipbuf_new(tid, settings.logger_buf_size);
    if (l->buf == NULL) {
        free(l);
        return NULL;
    }
    pthread_mutex_init(&l->mutex, NULL);
    return l;
} 

void sdrad_memcached_handle(void * c_copy)
{
    int     hv;
    conn *c = (conn *)c_copy;
    ms_data_ptr ->m_assoc_trusted_flag = 0; 
    if (ms_data_ptr -> sdrad_slabs_alloc_flag == 1){
        ms_data_ptr ->sdrad_slabs_alloc_flag  = 0; 
        ms_data_ptr ->trusted_it = do_item_alloc_pull(ms_data_ptr ->m_item_size,  ms_data_ptr ->m_slabs_id); 
        assert(ms_data_ptr ->trusted_it != 0x0);
        ms_data_ptr ->trusted_it -> it_flags = ms_data_ptr ->it_copy -> it_flags; 
        ms_data_ptr ->trusted_it -> exptime  = ms_data_ptr ->it_copy -> exptime;  
        ms_data_ptr ->trusted_it -> nbytes  = ms_data_ptr ->it_copy -> nbytes;  
        ms_data_ptr ->trusted_it -> nkey  = ms_data_ptr ->it_copy -> nkey;
        memcpy(ITEM_key(ms_data_ptr ->trusted_it), ITEM_key(ms_data_ptr ->it_copy), ms_data_ptr ->trusted_it->nkey);
        c->ritem = ITEM_data(ms_data_ptr ->trusted_it);
        c->rlbytes = ms_data_ptr ->trusted_it->nbytes;
        c->item = ms_data_ptr ->trusted_it;
    }
 
    if(ms_data_ptr ->m_store_it_flag == 1){   
        memcpy(ITEM_data(ms_data_ptr ->trusted_it ),ITEM_data(ms_data_ptr ->m_store_it_copy), ms_data_ptr ->trusted_it ->nbytes); 
        hv = hash(ITEM_key(ms_data_ptr ->trusted_it ), ms_data_ptr ->trusted_it ->nkey);

        __wrap_item_lock(hv);
        do_store_item(ms_data_ptr ->trusted_it, ms_data_ptr ->m_store_comm, c , hv);
        __wrap_item_unlock(hv);
    
        refcount_incr(ms_data_ptr ->trusted_it);
        ms_data_ptr ->m_store_it_flag = 0; 
        if (ms_data_ptr ->m_deleted_flag == 1){
        }
    
    }
    if( ms_data_ptr ->m_get_command == 1)
    {
        hv = ms_data_ptr -> m_hv_get_command;
        if (ms_data_ptr ->m_assoc_find_event_flag == 1){  
            __wrap_item_lock(hv);
            refcount_incr(ms_data_ptr ->m_get_it);
            ms_data_ptr ->m_assoc_find_event_flag = 0;
            __wrap_item_unlock(hv);
        };
        if(ms_data_ptr ->m_item_bump_flag == true){
            __wrap_item_lock(hv);
            do_item_bump(c, ms_data_ptr ->m_get_it, hv); 
            __wrap_item_unlock(hv);
            ms_data_ptr ->m_item_bump_flag = false;         
        }
        ms_data_ptr ->m_get_command = 0;
    }
    ms_data_ptr ->m_assoc_trusted_flag = 1; 
}

void m_passing_buffer_func(conn *c)
{
    ms_data_ptr ->trusted_it  = c -> item; 
    ms_data_ptr ->it_copy -> it_flags = ms_data_ptr ->trusted_it -> it_flags; 
    ms_data_ptr ->it_copy -> exptime  = ms_data_ptr ->trusted_it -> exptime ;  
    ms_data_ptr ->it_copy -> nbytes  = ms_data_ptr ->trusted_it -> nbytes;  
    ms_data_ptr ->it_copy -> nkey  = ms_data_ptr ->trusted_it -> nkey;
    ms_data_ptr ->it_copy -> refcount = ms_data_ptr ->trusted_it -> refcount;
    ms_data_ptr ->it_copy -> next = ms_data_ptr ->trusted_it -> next;
    memcpy(ITEM_key(ms_data_ptr ->it_copy),ITEM_key(ms_data_ptr ->trusted_it), ms_data_ptr ->trusted_it -> nkey);
    memcpy(ITEM_data(ms_data_ptr ->it_copy),ITEM_data(ms_data_ptr ->trusted_it), ms_data_ptr ->trusted_it -> nbytes);
    c-> item = ms_data_ptr ->it_copy;
    c-> ritem = ITEM_data(ms_data_ptr ->it_copy);
}

void m_initilazation_func(long udi)
{
    sdrad_dprotect(udi, MEMCACHED_HT_MUTEX_UDI, 0);
    sdrad_dprotect(udi, MEMCACHED_DATA_UDI, 0x2); 
    c_copy = sdrad_malloc(udi, sizeof(conn));
    thread_copy =  sdrad_malloc(udi, sizeof(LIBEVENT_THREAD));   
    sdrad_dprotect(0, MEMCACHED_DATA_UDI, 0);
    sdrad_dprotect(0, MEMCACHED_HT_MUTEX_UDI, 0);
    ms_data_ptr = sdrad_malloc(udi, sizeof(struct ms_database_integrity_s));       
    domain_log = domain_logger_create(udi);
    m_cache = m_cache_create(udi);
    m_initilazation = true;
}



