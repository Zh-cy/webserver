#ifndef __SQL__
#define __SQL__

#include <iostream>
#include <mysql/mysql.h>
#include <list>
#include <string.h>
#include <string>
#include <error.h>

#include "../locker/locker.h"
#include "../log/log.h"

using namespace std;

class sql_pool
{
public:
    MYSQL *GetConnection();
    bool ReleaseConnection(MYSQL *conn); // release one connection in the database connection pool
    static sql_pool* GetInstance(); // singleton
    int GetFreeConn(); // number of the connection in the database connection pool
    void Destroy(); // release the pool
    void init(string url, string user, string password, string dbname, int port, unsigned int MaxConn);

private:
    sql_pool();
    ~sql_pool();

private:
    unsigned int m_MaxConn; // max connection
    unsigned int m_CurConn; // cur connection
    unsigned int m_FreeConn; // free connection
    list<MYSQL *> connList; // database connection pool
    locker lock;
    sem reserve;

private:
    string m_url;
    string m_user;
    string m_password;
    string m_dbname;
    int m_port;
    int m_close_log; 
};

class ConnectionRAII
{
public:
   ConnectionRAII(MYSQL **conn, sql_pool *pool);
   ~ConnectionRAII();

private:
    MYSQL *RAIIconn;
    sql_pool *RAIIpool;
};
#endif
