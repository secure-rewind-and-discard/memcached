/**
 * @file memcached_with_sdrad.h
 * @author Merve Gülmez 
 * @brief 
 * @version 0.1
 * @date 2022-02-07
 * 
 * @copyright © Ericsson AB 2022-2023
 * 
 * SPDX-License-Identifier: BSD 3-Clause
 */

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
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


/* UDI */ 
#define MEMCACHED_DATA_UDI  2
#define MEMCACHED_HT_MUTEX_UDI 4

void __wrap_item_lock(uint32_t hv); 
void __wrap_item_unlock(uint32_t hv);
item *__real_do_item_alloc_pull(const size_t ntotal, const unsigned int id);

void *m_cache_create(unsigned long udi);
void m_initilazation_func(long tid); 
void m_passing_buffer_func(conn *c);

extern __thread struct ms_database_integrity_s *ms_data_ptr;
extern __thread void *m_cache; 
extern __thread int m_nested_domain_call; 
extern __thread int m_initilazation; 
extern __thread conn *c_copy;
extern __thread LIBEVENT_THREAD *thread_copy;
extern __thread logger *domain_log; 
extern __thread unsigned long tid;
extern __thread struct ms_database_integrity_s  ms_data; 

extern pthread_mutex_t *shared_item_locks;
void sdrad_memcached_handle(void * c_copy);
logger *domain_logger_create(unsigned long tid);

struct ms_database_integrity_s
{
    unsigned int     trusted_flag; 
    unsigned int     m_item_size; 
    unsigned int     m_slabs_id; 
    unsigned int     m_store_comm; 
    unsigned int     m_store_it_flag; 
    bool             m_item_bump_flag;
    unsigned int     m_get_command;  
    unsigned int     slabs_alloc_flag;
    unsigned int     sdrad_slabs_alloc_flag; 
    unsigned int     m_deleted_flag; 
    long             m_hv_get_command;
    unsigned int     m_assoc_trusted_flag; 
    unsigned int     m_assoc_find_it_flag; 
    unsigned int     m_assoc_find_event_flag; 
    unsigned int     m_store_find_init_flag;
    item             *it_copy; 
    item             *m_assoc_find_it;
    item             *trusted_it; 
    item             *m_store_it_copy;  
    item             *m_get_it;
}; 
