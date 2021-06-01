#ifndef __LOG__
#define __LOG__

#include <iostream>
#include <pthread.h>
#include <stdio.h>
#include <string.h> 

#include "block_queue.h"

using namespace std;


class Log
{
public:
    // lazy singleton
    static Log *getInstance()
    {
        static Log instance;
        return &instance;
    }
    
    // asynchronous log write method, call the private function: async_write_log()
    static void *flush_log_thread(void *args)
    {
        Log::getInstance()->async_write_log();
    }
    
    // parameters: log file name, buffer size, max lines, max log into queue(=0:synchron, >0 asynchron)
    bool init(const char *file_name, int log_buf_size = 512, int split_lines = 500, int max_queue_size = 0);

    void write_log(int level, const char *format, ...);

    void flush(void);

private:
    Log();
    virtual ~Log();
    void *async_write_log()
    {
        // this is for the string that comes from the log_queue
        string single_log;
        // m_log_queue is a simulated circular queue
        // pop(single_log) this parameter single_log is a outgoing parameter
        while (m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }
private:
    char dir_name[128]; // path name
    char log_name[128]; // file name
    int m_split_lines; // max lines of log
    int m_log_buf_size; // buffer size
    long long m_count; // current lines of log
    int m_today; // current date
    FILE *m_fp; // file pointer to the opened log
    char *m_buf;
    block_queue<string> *m_log_queue; // block queue
    bool m_is_async;
    locker m_mutex;
};


#define LOG_DEBUG(format, ...) Log::getInstance()->write_log(0, format, ##__VA_ARGS__);
#define LOG_INFO(format, ...) Log::getInstance()->write_log(1, format, ##__VA_ARGS__);
#define LOG_WARN(format, ...) Log::getInstance()->write_log(2, format, ##__VA_ARGS__);
#define LOG_ERROR(format, ...) Log::getInstance()->write_log(3, format, ##__VA_ARGS__);

#endif
