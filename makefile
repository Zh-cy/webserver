server: main.c ./threadpool/threadpool.h ./http/http.cpp ./http/http.h ./locker/locker.h ./log/log.cpp ./log/log.h ./log/block_queue.h ./sql/sql.cpp ./sql/sql.h
	g++ -o server main.c ./threadpool/threadpool.h ./http/http.cpp ./http/http.h ./locker/locker.h ./log/log.cpp ./log/log.h ./sql/sql.cpp ./sql/sql.h -lpthread -lmysqlclient
clean:
	rm -r server
