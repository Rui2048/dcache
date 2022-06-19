server : ./log/log.cpp ./master_service.cpp ./master_server.cpp
	g++ ./log/log.cpp ./master_service.cpp ./master_server.cpp -l pthread -o server