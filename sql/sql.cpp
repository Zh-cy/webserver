#include "sql.h"

using namespace std;

sql_pool::sql_pool()
{
    cout << "MYSQL constructor" << endl;
    this->m_FreeConn = 0;
    this->m_CurConn = 0;
}

sql_pool *sql_pool::GetInstance()
{
    static sql_pool conn_pool;
    return &conn_pool;
}


void sql_pool::init(string url, string user, string password, string dbname, int port, unsigned int MaxConn)
{
    this->m_url = url;
    this->m_user = user;
    this->m_password = password;
    this->m_dbname = dbname;

    lock.lock();
    for (int i = 0; i < MaxConn; i++)
    {
        MYSQL *con = NULL;
        con = mysql_init(con);
        if (con == NULL)
        {
            LOG_ERROR("MySQL init error");
            exit(1);
        }
        con = mysql_real_connect(con, url.c_str(), user.c_str(), password.c_str(), dbname.c_str(), port, NULL, 0);
        if (con == NULL)
        {
            LOG_ERROR("MySQL connect error");
            exit(1);
        }
        connList.push_back(con);
        ++m_FreeConn;
    }
    reserve = sem(m_FreeConn);
    this->m_MaxConn = m_FreeConn;
    lock.unlock();
}


MYSQL *sql_pool::GetConnection()
{
    MYSQL *con = NULL;
    if (connList.size() == 0)
        return NULL;
    reserve.wait();
    lock.lock();

    con = connList.front();
    connList.pop_front();

    --m_FreeConn;
    ++m_CurConn;

    lock.unlock();
    return con;
}


bool sql_pool::ReleaseConnection(MYSQL *conn)
{
    if (conn == NULL)
        return false;
    lock.lock();

    connList.push_back(conn);
    ++m_FreeConn;
    --m_CurConn;

    lock.unlock();
    reserve.post();
    return true;
}

void sql_pool::Destroy()
{
    lock.lock();
    if (connList.size()>0)
    {
        list<MYSQL *>::iterator it;
        for(it = connList.begin(); it != connList.end(); it++)
        {
            MYSQL *conn = *it;
            mysql_close(conn);
        }
        m_CurConn = 0;
        m_FreeConn = 0;
        connList.clear();
    }
    lock.unlock();
}

int sql_pool::GetFreeConn()
{
    return this->m_FreeConn;
}

sql_pool::~sql_pool()
{
    Destroy();
}

ConnectionRAII::ConnectionRAII(MYSQL **conn, sql_pool *pool)
{
    *conn = pool->GetConnection();
    RAIIconn = *conn;
    RAIIpool = pool;
}

ConnectionRAII::~ConnectionRAII()
{
    RAIIpool->ReleaseConnection(RAIIconn);
}
