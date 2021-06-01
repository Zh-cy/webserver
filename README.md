# webserver
Basic Information
------------------------------
* epoll (LT & ET + non-block IO)
* threadpool
* syn & asyn Log
* MySQL
* Sign in & Sign up
* timer to contrl the overtime client

Structure
------------------------------
<div align=left><img src="https://raw.githubusercontent.com/Zh-cy/images/master/WebServer/webserver.png" height="600" width="750" /> </div>

Config
------------------------------
* **MySQL** 
```sql
> -- create database testdb
> CREATE DATABASE testdb;
> 
> -- create table user
> CREATE TABLE IF NOT EXISTS `user` (
>   `username` VARCHAR(100) NOT NULL,
>   `passwd` VARCHAR(100) NOT NULL,
>   PRIMARY KEY (`username`)
>   )ENGINE=InnoDB;
>
> -- insert one user
> use testdb;
> INSERT INTO user(username, passwd) VALUES('user', '123456');
```

* **path in http.cpp** 
```cpp
// line 20
const char *doc_root = "/path/to/your/dir"(*)
```

* **MySQL in main.c** 
```cpp
// line 110
connPool->init("localhost", "root"(*), "root"(*), "testdb"(*), 3306, 8);
```

Run
------------------------------
* **compile**  
make

* **clean** 
make clean

* **launch**  
(8888 recommended)  
./server 8888
