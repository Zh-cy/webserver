#ifndef __THREADPOOL__
#define __THREADPOOL__

#include <iostream>
#include <pthread.h>
#include <list>
#include <exception>

#include "../sql/sql.h"
#include "../locker/locker.h"

using namespace std;

template <typename T>
class threadpool
{
public:
    threadpool(sql_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request);

private:
    static void *worker(void *arg);
    void run();

private:
    int m_thread_number; // number of threads
    int m_max_requests; // max number of the request queue
    pthread_t *m_threads; // thread pool array, size:m_thread_number
    list<T* > m_workqueue; // request queue
    locker m_queuelocker;
    sem m_queuestat;
    sql_pool *m_connPool;
    bool m_stop;
};

template <typename T>
threadpool<T>::threadpool(sql_pool *connPool, int thread_number, int max_request) : m_connPool(connPool), m_thread_number(thread_number), m_max_requests(max_request), m_threads(NULL), m_stop(false)
{
    if((thread_number <= 0) || (max_request <= 0))
         throw exception();
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw exception();
    for (int i = 0; i < thread_number; i++)
    {
        if (pthread_create(m_threads + i, NULL, worker, this)!=0) // dealing with the thread, this must be static, otherwise "this" point will be passed into the function which leads to error
                                                                // because this is a static function, so "this" pointer must be passed, otherwise other non static function or elements will not be inaccessible
        {
            delete[] m_threads;
            throw exception();
        }

        if (pthread_detach(m_threads[i])!=0)
        {
            delete[] m_threads;
            throw exception();
        }
    }

}

template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
    m_stop = true;
}

/*
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}
*/

template <typename T>
bool threadpool<T>::append(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run()
{
    while(!m_stop)
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)
            continue;

        ConnectionRAII mysqlcon(&request->mysql, m_connPool);
        
        request->process();
    }
}
#endif
