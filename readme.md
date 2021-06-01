#<center>WebServer</center>
## <center> Config MYSQL <center/>
alter user 'root'@'localhost' identified with mysql_native_password by 'root';
CREATE DATABASE testdb;
CREATE TABLE IF NOT EXISTS `user` (
	`username` VARCHAR(100) NOT NULL,
	`passwd` VARCHAR(100) NOT NULL,
	PRIMARY KEY (`username`)
	)ENGINE=InnoDB;
INSERT INTO user(username, passwd) VALUES('a1', 'p1');
-uroot -proot
database: testdb
table: user
	username VARCHAR(100) NOT NULL PRIMARY KEY
	passwd VARCHAR(100) NOT NULL

