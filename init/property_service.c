#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>

#include <sys/select.h>

/* According to earlier standards */
#include <sys/time.h>
#include <unistd.h>

#include <pthread.h>


#include "init.h"
#include "common.h"

#include "lxList.h"
#include "type.h"
#include "utils/property.h"

#include "property_service.h"

typedef struct property_node_s {
    struct lxlist_node node;
    struct property_data_s propertyVal;
}property_node_s;

typedef struct property_manager_s {
    property_node_s head;
    lxlist_Obj * gPropertyList;
}property_manager_t;

static property_manager_t * gPropertyManager = NULL;

static void property_set(struct property_data_s * propertyVal)
{
    if(propertyVal == NULL)
    {
        EPrint("property_set param is error\n");
        return PARAM_ERR;
    }

    struct property_node_s * PropertyNode = (struct property_node_s *)malloc(struct property_node_s);
    if(PropertyNode == NULL)
    {
        EPrint("malloc property_node failed, key:%s\n", propertyVal->property_name);
        return MEM_ERR;
    }

    memcpy(&PropertyNode->propertyVal, propertyVal, sizeof(struct property_data_s));

    LXLIST_ADD_HEAD(gPropertyManager->gPropertyList, &PropertyNode->node, &gPropertyManager->head.node);
}

I32 property_service_init(void)
{
    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(sockfd < NO_ERR)
    {
        EPrint("create sock failed\n");
        return SOCKET_ERR;
    }

    char * propertyEnv = getenv(PROPERTY_SERVICE_ENV);
    if(propertyEnv == NULL)
    {
        close(sockfd);
        EPrint("get property service env failed:%s\n", PROPERTY_SERVICE_ENV);
        return PARAM_ERR;
    }
    
    struct sockaddr_un myAddr;
    memset(&myAddr, 0x0, sizeof(struct sockaddr_un));
    myAddr.sun_family = AF_UNIX;
    strcpy(myAddr.sun_path, propertyEnv);

    if(bind(sockfd, (const struct sockaddr *)&myAddr, sizeof(struct sockaddr_un)) < NO_ERR)
    {
        close(sockfd);
        EPrint("bind unix socket failed:%s\n", myAddr.sun_path);
        return SOCKET_ERR;        
    }

    if(listen(sockfd, 5) < NO_ERR)
    {
        close(sockfd);
        EPrint("bind unix socket failed:%s\n", myAddr.sun_path);
        return SOCKET_ERR;
    }

    if(gPropertyManager == NULL)
    {
        gPropertyManager = (struct property_manager_t *)malloc(sizeof(struct property_manager_t));
        if((gPropertyManager != NULL) && ((gPropertyManager->gPropertyList = New(lxlist_Obj)) != NULL))
        {
            LXLIST_INIT(gPropertyManager->gPropertyList, &gPropertyManager->head.node)
        }
        else
        {
            if(gPropertyManager)
            {
                free(gPropertyManager);
                gPropertyManager = NULL;
                close(sockfd);

                return MEM_ERR;
            }
        }
    }

    return sockfd;
}

I32 property_service_handler(int epollfd, int sockfd)
{
    if(epollfd < NO_ERR || sockfd < NO_ERR)
    {
        EPrint("param error, please check!!!\n");
        return PARAM_ERR;
    }

    int fd = INVAILED_SOCKECT_FD;
    struct property_data_s propertyData;
    socklen_t socklen = sizeof(struct sockaddr);
    struct sockaddr ClientAddr;
    if((fd = accept(sockfd, &ClientAddr, &socklen)) < NO_ERR)
    {
        EPrint("accept failed\n");
        return SOCKET_ACCPET_ERR;
    }

    memset(&propertyData, 0x0, sizeof(struct property_data_s));
    int recvSize = TEMP_FAILURE_RETRY(recv(fd, (void *)&propertyData, sizeof(struct property_data_s), 0));
    if(recvSize != sizeof(struct property_data_s))
    {
        EPrint("recvSize:%d\n", recvSize);
        return SOCKET_ERR;
    }

    DPrint("action:%d, key:%s, val:%s, def:%s\n", propertyData.action, propertyData.property_name, propertyData.property_val, propertyData.default_val);

    switch(propertyData.action)
    {
        case SET_ACTION:
            property_set(&propertyData);
            break;
    }
}
