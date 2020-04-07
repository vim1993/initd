#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/epoll.h>

#include <signal.h>

#include "init.h"
#include "common.h"

#include "property_service.h"

#define EPOLL_WAIT_TIMEOUT 500
#define SUPPORT_MAX_EVENTS 1024

#define INIT_PROCESS_VER "1.0.1"

void sighandler(int sig)
{
    //IPrint("recv signal %d\n", sig);

    return;
}


int init_main(int argc, char **argv)
{
    int index = 0;
    int occurFd = NO_ERR;

    IPrint("Initd Process start\n");
    IPrint("initd Version:%s\n", INIT_PROCESS_VER);
    //1. start property_thread for recv & send msg
    int propertySock = property_service_init();
    if(propertySock < NO_ERR)
    {
        EPrint("init main, create property service socket failed\n");
        return SOCKET_ERR;
    }

    //2. create epoll
    struct epoll_event env;
    struct epoll_event gEvent[SUPPORT_MAX_EVENTS];
    int epollFd = epoll_create(5);
    if(epollFd < NO_ERR)
    {
        close(propertySock);
        EPrint("epoll_create failed\n");
        return OS_ERR;
    }

    env.data.fd = propertySock;
    env.events = EPOLLIN;
    //3. add epoll ctrl
    if(epoll_ctl(epollFd, EPOLL_CTL_ADD, propertySock, &env) < NO_ERR)
    {
        close(propertySock);
        EPrint("epoll_create failed\n");
        return OS_ERR;
    }

    #ifdef ReleaseDebug
    char exitChar = 0;

    memset(&env, 0x0, sizeof(struct epoll_event));
    env.data.fd = STDIN_FILENO;
    env.events = EPOLLIN;
    //3. add epoll ctrl
    if(epoll_ctl(epollFd, EPOLL_CTL_ADD, STDIN_FILENO, &env) < NO_ERR)
    {
        close(propertySock);
        EPrint("epoll_create failed\n");
        return OS_ERR;
    }
    #endif
    
    signal(SIGPIPE, sighandler);

    while(1)
    {
        occurFd = epoll_wait(epollFd, gEvent, SUPPORT_MAX_EVENTS, EPOLL_WAIT_TIMEOUT);
        if(occurFd == 0)
        {
            continue;
        }
        else if(occurFd < 0)
        {
            EPrint("epoll_wait occurFd :%d, errno:%d\n", occurFd, errno);
            break;
        }
        
        for(index = 0; index < occurFd; index++)
        {
            if(gEvent[index].events == EPOLLIN)
            {
                if(gEvent[index].data.fd == propertySock)
                {
                    property_service_handler(epollFd, propertySock);
                }
                #ifdef ReleaseDebug
                else if(gEvent[index].data.fd == STDIN_FILENO)
                {
                    scanf("%c", &exitChar);
                    //IPrint("exitChar:%c, %d\n", exitChar, (exitChar == 'q'));
                    if(exitChar == 'q')
                    {
                        goto exit;
                    }
                }
                #endif
            }
        }
    }
    
exit:
    property_service_uninit();
    return NO_ERR;
}
