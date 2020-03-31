#ifndef __COMMOM_H__
#define __COMMOM_H__

#include <errno.h>

#define INVAILED_SOCKECT_FD -1

#define NO_ERR 0
#define PARAM_ERR -1

//FILE ERROR, contains open failed, read/write failed
#define FILE_ERR -2
//MEM ERROR, contains memcpy failed, malloc failed
#define MEM_ERR -3

//SOCKET ERROR, contains socket create failed, bind failed, listern failed
#define SOCKET_ERR -4

#define SOCKET_ACCPET_ERR -5
#define SOCKET_RECV_ERR -6
#define SOCKET_SEND_ERR -7

//OS ERR, contains pthread creat failed, mutex create failed
#define OS_ERR -100

#define TEMP_FAILURE_RETRY(exp) ({        \
    typeof (exp) _val;                    \
    do{                                   \
        _val = (exp);                     \
    }while(_val == -1 && errno == EINTR); \
    _val; })

typedef int I32;
typedef unsigned int U32;

typedef short I16;
typedef unsigned short U16;

typedef char I8;
typedef unsigned char U8;

typedef void * VOIDPTR;

#define IPrint(formate, ...) printf(formate, ##__VA_ARGS__)
#define VPrint(formate, ...) printf(formate, ##__VA_ARGS__)
#define DPrint(formate, ...) printf("[D]"formate, ##__VA_ARGS__)
#define EPrint(formate, ...) printf("[E][%s][%d]"formate, __func__, __LINE__, ##__VA_ARGS__)

#define FUNC_IN() printf("[%s][%d], func in\n", __func__, __LINE__)
#define FUNC_OUT() printf("[%s][%d], func out\n", __func__, __LINE__)

#endif // !__COMMOM_H__