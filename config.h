#ifndef CONFIG_H_
#define CONFIG_H_

#include <unistd.h>
#include <stdlib.h>

//cache_server的通信码
#define MOVE_DATA "move_data"
#define SET_VALUE "set_value"
#define SET_BACKUP_VALUE "set_backup_value"
#define GET_VALUE "get_value"
#define GET_BACKUP_VALUE "get_backup_value"
#define DIRTY_DATA "dirty_data"
#define DATA_NOT_EXIST "data_not_exist"
#define GET_SUCCESS "get_success"

//解析命令行参数
class Config
{
public:
    Config();
    ~Config(){};

    void parse_arg(int argc, char*argv[]);

    //端口号
    int PORT;

};


#endif //end of CONFIG_H