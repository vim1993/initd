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

#include "lxOs.h"
#include "lxMsgQue.h"

#include "lxList.h"
#include "type.h"
#include "utils/property.h"

#include "property_service.h"

#define PERSIST_PROPERTY "persist."

typedef struct property_handler_s {
    int (*handler_callback)(struct property_handler_s * pThis);
}property_handler_s;

typedef struct property_observer_handler_s {
    struct property_handler_s gBaseHandler;
    struct property_data_s propertyVal;
}property_observer_handler_s;

typedef struct property_node_s {
    struct lxlist_node node;
    struct property_data_s propertyVal;
}property_node_s;

typedef struct property_observer_list {
    int sockfd;
    struct lxlist_node node;
    char key[NAME_MAX];
    int (*checkAndSend)(struct property_observer_list * pThis, struct property_data_s * changeProperty);
}property_observer_list_t;

typedef struct property_manager_s {
    property_observer_list_t list;
    lxlist_Obj * gObserverList;
    mutex_lock * glock;
    pthread_t gObserverTid;

    msgque_obj * gMsgQue;
    
    property_node_s head;
    lxlist_Obj * gPropertyList;
}property_manager_t;

static property_manager_t * gPropertyManager = NULL;

static property_node_s * property_query(const char * key)
{
    if(key == NULL)
    {
        EPrint("property_set param is error\n");
        return NULL;
    }

    struct property_node_s * PropertyNode = &gPropertyManager->head;
    while(1)
    {
        if(strcmp(PropertyNode->propertyVal.property_name, key) == 0)
        {
            return PropertyNode;
        }

        PropertyNode = GET_STRUCT_HEAD_PTR(property_node_s, PropertyNode->node.Next, node);
        if(PropertyNode == NULL || PropertyNode == &gPropertyManager->head)
        {
            break;
        }
    }

    return NULL;
}

static struct property_observer_list * observer_query(int sockfd, const char * key)
{
    if(sockfd < NO_ERR || key == NULL)
    {
        EPrint("property_set param is error\n");
        return NULL;
    }

    struct property_observer_list * ObserverNode = &gPropertyManager->list;
    while(1)
    {
        if(strcmp(ObserverNode->key, key) == 0 && ObserverNode->sockfd == sockfd)
        {
            return ObserverNode;
        }

        ObserverNode = GET_STRUCT_HEAD_PTR(property_observer_list_t, ObserverNode->node.Next, node);
        if(ObserverNode == NULL || ObserverNode == &gPropertyManager->list)
        {
            break;
        }
    }

    return NULL;
}

static void * property_observer_proc(void * param)
{
    if(param == NULL)
    {
        EPrint("param error, please check\n");
        return NULL;
    }

    int sendSize = NO_ERR;
    struct property_handler_s * handler = NULL;
    property_manager_t * manager = (property_manager_t *)param;
    while(1)
    {
        handler = manager->gMsgQue->pop_front(manager->gMsgQue);
        if(handler == NULL)
        {
            continue;
        }

        FUNC_IN();
        handler->handler_callback(handler);
        manager->gMsgQue->release_buffer(manager->gMsgQue, handler);
        FUNC_OUT();
    }
}

static int property_handler_callback(struct property_handler_s * pThis)
{
    if(pThis == NULL)
    {
        return PARAM_ERR;
    }

    FUNC_IN();
    int sendSize = NO_ERR;
    property_observer_handler_s * handler = (property_observer_handler_s *)pThis;
    struct property_observer_list * ObserverNode = &gPropertyManager->list;
    while(1)
    {
        if(strcmp(ObserverNode->key, handler->propertyVal.property_name) == 0)
        {
            sendSize = ObserverNode->checkAndSend(ObserverNode, &handler->propertyVal);
            if(sendSize != NO_ERR)
            {
                PMUTEX_LOCK(gPropertyManager->glock);
                LXLIST_DEL_NODE(gPropertyManager->gObserverList, &ObserverNode->node);
                PMUTEX_UNLOCK(gPropertyManager->glock);
            }
        }

        ObserverNode = GET_STRUCT_HEAD_PTR(property_observer_list_t, ObserverNode->node.Next, node);
        if(ObserverNode == NULL || ObserverNode == &gPropertyManager->list)
        {
            break;
        }
    }

    FUNC_OUT();
    
    return NO_ERR;
}

static int property_observer_callback(struct property_observer_list * pThis, struct property_data_s * changeProperty)
{
    if(pThis == NULL)
    {
        EPrint("param is err, please check\n");
        return PARAM_ERR;
    }

    int sendSize = NO_ERR;

    FUNC_IN();
    IPrint("property_name:%s,%s\n", changeProperty->property_name, pThis->key);
    if(strcmp(changeProperty->property_name, pThis->key) == 0)
    {
        changeProperty->action = OBS_ACTION;
        sendSize = TEMP_FAILURE_RETRY(send(pThis->sockfd, (const void *)changeProperty, sizeof(struct property_data_s), 0));
        if(sendSize != sizeof(struct property_data_s))
        {
            DPrint("send observer failed-->KEY:%s<--\n", pThis->key);
            return SOCKET_SEND_ERR;
        }
    }
    FUNC_OUT();

    return NO_ERR;
}

static int property_observer_register(int sockfd, const char * key)
{
    if(gPropertyManager == NULL || sockfd < NO_ERR || key == NULL)
    {
        EPrint("param error\n");
        return PARAM_ERR;
    }

    struct property_observer_list * observerNode = observer_query(sockfd, key);
    if(observerNode != NULL)
    {
        return NO_ERR;
    }
    
    observerNode = (struct property_observer_list *)malloc(sizeof(struct property_observer_list));
    if(observerNode == NULL)
    {
        EPrint("param error\n");
        return MEM_ERR;
    }

    strcpy(observerNode->key, key);
    observerNode->checkAndSend = property_observer_callback;
    observerNode->sockfd = sockfd;
    LXLIST_ADD_HEAD(gPropertyManager->gObserverList, &observerNode->node, &gPropertyManager->list.node);

    return NO_ERR;
}

static I8 property_service_set(struct property_data_s * propertyVal)
{
    if(propertyVal == NULL)
    {
        EPrint("property_set param is error\n");
        return PARAM_ERR;
    }

    struct property_node_s * queryNode = property_query(propertyVal->property_name);

    if(queryNode != NULL)
    {
        memcpy(&queryNode->propertyVal, propertyVal, sizeof(struct property_data_s));
    }
    else
    {
        struct property_node_s * PropertyNode = (struct property_node_s *)malloc(sizeof(struct property_node_s));
        if(PropertyNode == NULL)
        {
            EPrint("malloc property_node failed, key:%s\n", propertyVal->property_name);
            return MEM_ERR;
        }

        memcpy(&PropertyNode->propertyVal, propertyVal, sizeof(struct property_data_s));

        LXLIST_ADD_HEAD(gPropertyManager->gPropertyList, &PropertyNode->node, &gPropertyManager->head.node);
    }

    DPrint("key:%s, val:%s\n", propertyVal->property_name, propertyVal->property_val);

    if(memcmp(propertyVal->property_name, PERSIST_PROPERTY, 8) == 0)
    {
        //do persist save
        
    }

    struct property_observer_handler_s handler;
    handler.gBaseHandler.handler_callback = property_handler_callback;
    memcpy(&handler.propertyVal, propertyVal, sizeof(struct property_data_s));
    gPropertyManager->gMsgQue->push_back(gPropertyManager->gMsgQue, &handler, sizeof(struct property_observer_handler_s));
    
    return NO_ERR;
}

static I8 property_service_get(struct property_data_s * propertyVal)
{
    if(propertyVal == NULL)
    {
        EPrint("property_set param is error\n");
        return PARAM_ERR;
    }

    struct property_node_s * queryNode = property_query(propertyVal->property_name);
    if(queryNode)
    {
        memcpy(propertyVal, &queryNode->propertyVal, sizeof(struct property_data_s));
        return NO_ERR;
    }

    return PARAM_ERR;
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

    DPrint("propertyEnv:%s\n", propertyEnv);
    if(access(propertyEnv, F_OK) == F_OK)
    {
        unlink(propertyEnv);
        remove(propertyEnv);
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
        gPropertyManager = (struct property_manager_s *)malloc(sizeof(struct property_manager_s));
        if((gPropertyManager != NULL) && ((gPropertyManager->gPropertyList = New(lxlist_Obj)) != NULL)
            && ((gPropertyManager->gObserverList = New(lxlist_Obj)) != NULL)
            && (gPropertyManager->glock = pthread_resource_lock_new()) != NULL 
            && (gPropertyManager->gMsgQue = New(msgque_obj)) != NULL)
        {
            LXLIST_INIT(gPropertyManager->gPropertyList, &gPropertyManager->head.node);
            LXLIST_INIT(gPropertyManager->gObserverList, &gPropertyManager->list.node);

            pthread_create(&gPropertyManager->gObserverTid, NULL, property_observer_proc, (void *)gPropertyManager);
        }
        else
        {
            if(gPropertyManager)
            {
                if(gPropertyManager->gPropertyList)
                {
                    DELETE(lxlist_Obj, gPropertyManager->gPropertyList);
                }

                if(gPropertyManager->gObserverList)
                {
                    DELETE(lxlist_Obj, gPropertyManager->gObserverList);
                }

                if(gPropertyManager->glock)
                {
                    pthread_resource_lock_delete(gPropertyManager->glock);
                }

                if(gPropertyManager->gMsgQue)
                {
                    DELETE(msgque_obj, gPropertyManager->gMsgQue);
                }
            
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
    int Size = TEMP_FAILURE_RETRY(recv(fd, (void *)&propertyData, sizeof(struct property_data_s), 0));
    if(Size != sizeof(struct property_data_s))
    {
        EPrint("recvSize:%d\n", Size);
        return SOCKET_ERR;
    }

    DPrint("action:%d, key:%s, val:%s, def:%s\n", propertyData.action, propertyData.property_name, propertyData.property_val, propertyData.default_val);

    I8 result = PARAM_ERR;
    switch(propertyData.action)
    {
        case SET_ACTION:
            result = property_service_set(&propertyData);
            break;

        case GET_ACTION:
            result = property_service_get(&propertyData);
            propertyData.action = GET_ACTION;
            break;

        case OBR_ACTION:
            result = property_observer_register(fd, (const char *)propertyData.property_name);
            break;
    }

    DPrint("key:%s, result:%d\n", propertyData.property_name, result);

    if(propertyData.action != OBR_ACTION)
    {
        if(result == NO_ERR && propertyData.action == GET_ACTION)
        {
            Size = TEMP_FAILURE_RETRY(send(fd, (void *)&propertyData, sizeof(struct property_data_s), 0));
            if(Size != sizeof(struct property_data_s))
            {
                EPrint("send result failed\n");
                close(fd);
                return SOCKET_ERR;
            }
        }

        close(fd);
    }
    else if(result != NO_ERR && propertyData.action == OBR_ACTION)
    {
        close(fd);
    }

    return NO_ERR;
}
