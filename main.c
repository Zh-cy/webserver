#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>

#include "./locker/locker.h"
#include "./threadpool/threadpool.h"
#include "./timer/lst_timer.h"
#include "./http/http.h"
#include "./log/log.h"
#include "./sql/sql.h"

#define MAX_FD 65536 // max file descriptor
#define MAX_EVENT_NUMBER 10000  // max events
#define TIMESLOT 5

#define SYNLOG // synchron log
// #define ASYNLOG // asynchron log

// #define listenfdET // ET non block
#define listenfdLT // LT block

// defined in http.cpp
extern int addfd(int epollfd, int fd, bool one_shot);
// extern int remove(int epollfd, int fd);
extern int setnonblocking(int fd);

// timer parameters
static int pipefd[2];
static sort_timer_lst timer_lst;
static int epollfd = 0;

// signal handle function
void sig_handler(int sig)
{
    // re-entrancy of the function, keep the original errno
    int save_errno = errno;
    int msg = sig;
    // use send() function, send the captured signal into pipe
    send(pipefd[1], (char *)&msg, 1, 0);
    // assign the saved save_errno to errno -> re-entrancy
    errno = save_errno;
}

// signal function
void addsig(int sig, void(handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

// set alarm, every timeslot send a SIGALRM signal
void timer_handler()
{
    timer_lst.tick();
    alarm(TIMESLOT);
}

// timer call back function, delete the event on the socket which is inactive and close it
void cb_func(client_data *user_data)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
    LOG_INFO("close fd %d", user_data->sockfd);
    Log::getInstance()->flush();
}

void show_error(int connfd, const char *info)
{
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char *argv[])
{
#ifdef ASYNLOG
    Log::getInstance()->init("Log", 2048, 800000, 8);
#endif

#ifdef SYNLOG
    Log::getInstance()->init("Log", 2048, 800000, 0);
#endif

    if(argc <= 1)
    {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    
    int port = atoi(argv[1]);

    // set SIGPIPE signal, because when we call twice "write" function to the closed socket, the second write will lead to SIGPIPE signal and the default action of SIGPIPE is to close the process
    addsig(SIGPIPE, SIG_IGN);
    
    // create database connection poll
    sql_pool *connPool = sql_pool::GetInstance();
    connPool->init("localhost", "root", "root", "testdb", 3306, 8);

    // thread pool
    threadpool<http_conn> *pool = NULL;
    try
    {
        pool = new threadpool<http_conn>(connPool);
    }
    catch (...)
    {
        return 1;
    }

    http_conn *users = new http_conn[MAX_FD];
    assert(users);

    // init global map which stores the username and password
    users->initmysql_result(connPool);
    // create the listenfd
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    // struct linger tmp = {1, 0}; // {int l_onoff, int l_linger}
    // l_onoff: on/off, l_linger: set delay time, if there is still data in the buffer need to be sent, then after "l_linger" seconds it will be closed
    // setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));
    
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    int flag = 1;
    // port reuse
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);
    ret = listen(listenfd, 5);
    assert(ret >= 0);

    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5); 
    assert(epollfd != -1);

    // main thread is in charge of listening if there is a new client connection
    addfd(epollfd, listenfd, false);
    // 之后对于http事件肯定要用到句柄epollfd，所以http内部要定义一个m_epollfd来接收epollfd。m_epollfd为静态成员变量，因为只有唯一一个句柄
    http_conn::m_epollfd = epollfd; // 这里一个疑问，不用给地址吗？ 可能是因为一个进程内文件描述符就是0-1023，而epollfd肯定对应唯一一个数字，所以地址无所谓，主要是看哪个数字对应epollfd

    // create pipe to transmit signal
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    // this signal pipe can be non block. Because this is only to deal with the time out signal, evenif that buffer is full is not matter, this has no bad affect to the server
    setnonblocking(pipefd[1]);
    // this pipe also need to be added into the epoll tree
    addfd(epollfd, pipefd[0], false);

    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);
    bool stop_server = false;

    client_data *users_timer = new client_data[MAX_FD];
    
    bool timeout = false;
    // TIMESLOT后开始发SIGALRM信号，信号被捕捉后，信号处理函数内部还有alarm(TIMESLOT)，这样每隔TIMESLOT后都会发送信号
    alarm(TIMESLOT);

    while(!stop_server)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            // new client connection
            if (sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
#ifdef listenfdLT
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);
                if (connfd < 0)
                {
                    LOG_ERROR("%s:errno is:%d", "listenfdLT accept error", errno);
                    continue;
                }
                if (http_conn::m_user_count >= MAX_FD)
                {
                    show_error(connfd, "Internal server busy");
                    LOG_ERROR("%s", "Internal server busy");
                    continue;
                }
                users[connfd].init(connfd, client_address);
                
		// init client_data
                // create timer, set callback function and timeout seconds, bind the client data, insert this timer into the list
                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd = connfd;
                util_timer *timer = new util_timer;
                timer->user_data = &users_timer[connfd]; // client_data that the callback function needs
                timer->cb_func = cb_func; // register callback function
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users_timer[connfd].timer = timer;
                timer_lst.add_timer(timer);
#endif

#ifdef listenfdET
                while(1)
                {
                    int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);
                    if (connfd < 0)
                    {
                        LOG_ERROR("%s:errno is:%d", "listenfdET accept error", errno);
                        break;
                    }
                    if(http_conn::m_user_count >= MAX_FD)
                    {
                        show_error(connfd, "Internal server busy");
                        LOG_ERROR("%s", "Internal server busy");
                        break;
                    }
                    users[connfd].init(connfd, client_address);

                    // init client_data
                    // create timer, set callback function and timeout seconds, bind the client data, insert this timer into the list
                    users_timer[connfd].address = client_address;
                    users_timer[connfd].sockfd = connfd;
                    util_timer *timer = new util_timer;
                    timer->user_data = &users_timer[connfd]; // client_data that the callback function needs
                    timer->cb_func = cb_func; // register callback function
                    time_t cur = time(NULL);
                    timer->expire = cur + 3 * TIMESLOT;
                    users_timer[connfd].timer = timer;
                    timer_lst.add_timer(timer);
                }
                continue;
#endif
            }

            // 处理出错
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                // error occurs, server closes the connection, remove timer
                util_timer *timer = users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);

                if (timer)
                {
                    timer_lst.del_timer(timer);
                }
            }

            // dealing with signal
            else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1)
                    continue;
                else if (ret == 0)
                    continue;
                else
                {
                    for (int i = 0; i < ret; i++)
                    {
                        switch(signals[i])
                        {
                        case SIGALRM:
                        {
                            timeout = true;
                            break;
                        }
                        case SIGTERM:
                        {
                            stop_server = true;
                        }
                        }
                    }
                }
            }

            // client send http request data, server deals with this received data
            else if (events[i].events & EPOLLIN)
            {
                // take this sockfd corresponding timer
                util_timer *timer = users_timer[sockfd].timer;
                if (users[sockfd].read_once())
                {
                    LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::getInstance()->flush();
                    // if read event checked, then insert it into the request queue, every sockfd matches the only one client
                    pool->append(users + sockfd);

                    // if there is data transmission, then delay the timer by 3 timeslots, adjust the position of the new timer on the list
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::getInstance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                }
                else
                {
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
            // dealing with the write events, that is log.html，register.html etc. the responce message
            else if (events[i].events & EPOLLOUT)
            {
                util_timer *timer = users_timer[sockfd].timer;
                if (users[sockfd].write())
                {
                    LOG_INFO("send data to client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
                    Log::getInstance()->flush();
                    
                    // if there is data transmission, then delay the timer by 3 timeslots, adjust the position of the new timer on the list
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::getInstance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                }
                else
                {
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }
            }
        }
        if (timeout)
        {
            // if there is SIGALRM signal in this loop of events list, then timeout is true, then we need to deal with the possible timeout task with tick(), and set alarm(TIMESLOT)
            // after TIMESLOT send SIGALRM signal, set timeout as false
            timer_handler();
            timeout = false;
        }
        // if there is SIGTERM signal in this loop of events list, then stop_server is true, while loop goes to end, and close the server
    }
    close(epollfd);
    close(listenfd);
    close(pipefd[0]);
    close(pipefd[1]);
    delete[] users;
    delete[] users_timer;
    delete pool;
    return 0;
}
