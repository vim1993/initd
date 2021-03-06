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
#include "type.h"
#include "lxMsgQue.h"

#include "utils/property.h"

typedef struct property_observer_Node_s {
    char * property;
    property_observer_t * mObserver;

    struct property_observer_Node_s * next;
}property_observer_Node_s;

typedef struct ResolverManager_s{
    int mSockfd;
    int mStopObserver;
    pthread_t mTid;
    pthread_t mDispatch_tid;
    struct mutex_lock * glock;
    struct msgque_obj * msgQue;
    property_observer_Node_s * head;
    PropertyResolver gResolver;
}ResolverManager_s;

static ResolverManager_s * gResolverManager = NULL;

static int property_socket_init(void)
{
    int fd = INVAILED_SOCKECT_FD;
    struct sockaddr_un serverAddr;
    char * unixPath = getenv(PROPERTY_SERVICE_ENV); //"/tmp/property_service.unix";
    if(unixPath == NULL || unixPath == "")
    {
        EPrint("get %s failed\n", PROPERTY_SERVICE_ENV);
        return PARAM_ERR;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if(fd < NO_ERR)
    {
        EPrint("create socket failed\n");
        return PARAM_ERR;
    }

    memset(&serverAddr, 0x0, sizeof(struct sockaddr_un));
    serverAddr.sun_family = AF_UNIX;
    strcpy(serverAddr.sun_path, unixPath);
    if(connect(fd, (const struct sockaddr *)&serverAddr, sizeof(struct sockaddr_un)) < NO_ERR)
    {
        EPrint("connect unix sockect %s failed\n", serverAddr.sun_path);
        close(fd);
        return SOCKET_ERR;
    }

    return fd;
}

static int onPropertyValChange(struct property_observer_t * pThis, char * property_name, char * property_val)
{
    IPrint("property_name:%s, property_val:%s\n", property_name, property_val);

    return NO_ERR;
}

static void property_dispatch(ResolverManager_s * manager, property_data_s * propertyVal)
{
    if(manager == NULL || propertyVal == NULL)
    {
        EPrint("param error\n");
        return;
    }

    property_observer_Node_s * observerNode = NULL;

    PMUTEX_LOCK(manager->glock);

    observerNode = manager->head;

    while(observerNode != NULL)
    {
        if(strcmp(propertyVal->property_name, observerNode->property) == 0)
        {
            if(observerNode->mObserver->onPropertyValChange)
            {
                observerNode->mObserver->onPropertyValChange(observerNode->mObserver, propertyVal->property_name, propertyVal->property_val);
            }
            else
            {
                onPropertyValChange(observerNode->mObserver, propertyVal->property_name, propertyVal->property_val);
            }
        }

        observerNode = observerNode->next;
    }

    PMUTEX_UNLOCK(manager->glock);

    return;
}

static void * property_dispatch_proc(void * param)
{
    if(param == NULL)
    {
        EPrint("param error, please check!\n");
        return NULL;
    }


    unsigned int index = 0;
    int iret = PARAM_ERR;

    property_data_s * propertyVal;

    ResolverManager_s * gObserverManager = (ResolverManager_s *) param;
    while(!gObserverManager->mStopObserver)
    {
        propertyVal = gObserverManager->msgQue->pop_front_timeout(gObserverManager->msgQue, 1);
        if(propertyVal != NULL)
        {
            property_dispatch(gObserverManager, propertyVal);
            gObserverManager->msgQue->release_buffer(gObserverManager->msgQue, propertyVal);
        }
    }

    IPrint("[%s] pthread exit\n", __func__);

    return NULL;
}

static void * property_observer_proc(void * param)
{
    if(param == NULL)
    {
        EPrint("param error, please check!\n");
        return NULL;
    }


    unsigned int index = 0;
    int iret = PARAM_ERR;

    property_data_s propertyVal;

    ResolverManager_s * gObserverManager = (ResolverManager_s *) param;
    struct timeval timeout;
    

    fd_set rfds, tfds;
    FD_ZERO(&tfds);
    FD_SET(gObserverManager->mSockfd, &tfds);
    
    while(!gObserverManager->mStopObserver)
    {
        
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        rfds = tfds;
        
        iret = select(gObserverManager->mSockfd + 1, &rfds, NULL, NULL, &timeout);
        if(iret == 0)
        {
            continue;
        }
        else if(iret < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            EPrint("select recv error, break\n");
            break;
        }

        if(FD_ISSET(gObserverManager->mSockfd, &rfds))
        {
            memset(&propertyVal, 0x0, sizeof(property_data_s));
            iret = TEMP_FAILURE_RETRY(recv(gObserverManager->mSockfd, (void *)&propertyVal, sizeof(property_data_s), 0));
            if(iret > 0)
            {
                if(propertyVal.action == OBS_ACTION)
                {
                    if(gObserverManager->msgQue != NULL && gObserverManager->mDispatch_tid != 0)
                    {
                        gObserverManager->msgQue->push_back(gObserverManager->msgQue, (void *)&propertyVal, sizeof(property_data_s));
                    }
                    else
                    {
                        property_dispatch(gObserverManager, &propertyVal);
                    }
                }
            }
        }
    }

    FD_CLR(gObserverManager->mSockfd, &tfds);
    FD_ZERO(&tfds);

    IPrint("[%s] pthread exit\n", __func__);
    
    return NULL;
}

static int PropertyResolver_init(ResolverManager_s * manager)
{
    if(manager == NULL)
    {
        DPrint("register param is error\n");
        return PARAM_ERR;
    }

    int iret = PARAM_ERR;
    pthread_t tid, dispatch_tid;
    int fd = property_socket_init();
    if(fd < NO_ERR)
    {
        return fd;
    }

    manager->mSockfd = fd;
    if(pthread_create(&tid, NULL, property_observer_proc, (void *)manager) < NO_ERR)
    {
        close(fd);
        EPrint("pthread_create failed\n");
        return OS_ERR;
    }
    manager->mTid = tid;

    if(pthread_create(&dispatch_tid, NULL, property_dispatch_proc, (void *)manager) < NO_ERR)
    {
        EPrint("pthread_create failed\n");
        manager->mDispatch_tid = 0;
    }
    else
    {
        manager->mDispatch_tid = dispatch_tid;
    }

    return NO_ERR;
}

static int RegisterObserver(struct PropertyResolver * pThis, char * property_name, struct property_observer_t * observer)
{
    if(pThis == NULL || property_name == NULL || observer == NULL)
    {
        return PARAM_ERR;
    }

    ResolverManager_s * resolverManager = GET_STRUCT_HEAD_PTR(ResolverManager_s, pThis, gResolver);
    if(resolverManager == NULL)
    {
        return PARAM_ERR;
    }

    PMUTEX_LOCK(resolverManager->glock);

    property_observer_Node_s * node = (property_observer_Node_s *)malloc(sizeof(property_observer_Node_s));
    if(node == NULL)
    {
        EPrint("malloc property_observer_block_s failed\n");
        return MEM_ERR;
    }

    node->mObserver = observer;
    node->property = property_name;

    if(resolverManager->head == NULL)
    {
        resolverManager->head = node;
    }
    else
    {
        node->next = resolverManager->head->next;
        resolverManager->head = node;
    }

    property_data_s propertyVal;
    memset(&propertyVal, 0x0, sizeof(propertyVal));
    propertyVal.action = OBR_ACTION;
    strcpy(propertyVal.property_name, property_name);
    
    int iret = TEMP_FAILURE_RETRY(send(resolverManager->mSockfd, &propertyVal, sizeof(property_data_s), 0));
    if(iret < NO_ERR)
    {
        return SOCKET_SEND_ERR;
    }

    PMUTEX_UNLOCK(resolverManager->glock);

    return NO_ERR;
}

static int UnRegisterObserver(struct PropertyResolver * pThis, char * property_name, struct property_observer_t * observer)
{
    if(pThis == NULL || property_name == NULL || observer == NULL)
    {
        return PARAM_ERR;
    }

    ResolverManager_s * resolverManager = GET_STRUCT_HEAD_PTR(ResolverManager_s, pThis, gResolver);
    if(resolverManager == NULL)
    {
        return PARAM_ERR;
    }

    PMUTEX_LOCK(resolverManager->glock);

    property_observer_Node_s * node = resolverManager->head;

    while(node != NULL)
    {
        if(node->mObserver == observer && strcmp(node->property, property_name) == 0)
        {
            resolverManager->head = node->next;
            free(node);
            node = resolverManager->head;
        }

        if(node == NULL)
        {
            break;
        }
        
        node = node->next;
    }

    PMUTEX_UNLOCK(resolverManager->glock);

    return NO_ERR;
}

PropertyResolver * getPropertyResolver()
{
    if(gResolverManager == NULL)
    {
        gResolverManager = (ResolverManager_s *)malloc(sizeof(ResolverManager_s));

        if(gResolverManager != NULL)
        {
            gResolverManager->glock = pthread_resource_lock_new();
            if(gResolverManager->glock == NULL)
            {
                free(gResolverManager);
                EPrint("pthread_resource_lock_new failed\n");
                return NULL;
            }
            gResolverManager->msgQue = New(msgque_obj);
            gResolverManager->mStopObserver = 0;
            gResolverManager->gResolver.RegisterObserver = RegisterObserver;
            gResolverManager->gResolver.UnRegisterObserver = UnRegisterObserver;

            if(PropertyResolver_init(gResolverManager)!= NO_ERR)
            {
                if(gResolverManager->msgQue)
                {
                    DELETE(msgque_obj, gResolverManager->msgQue);
                }
                pthread_resource_lock_delete(gResolverManager->glock);
                free(gResolverManager);

                EPrint("PropertyResolver failed\n");
                return NULL;
            }
        }
    }

    return &gResolverManager->gResolver;
}

void releasePropertyResolver()
{
    property_observer_Node_s * node = NULL;

    if(gResolverManager != NULL)
    {
        gResolverManager->mStopObserver = 1;
        pthread_join(gResolverManager->mTid, NULL);
        if(gResolverManager->mDispatch_tid)
        {
            pthread_join(gResolverManager->mDispatch_tid, NULL);
        }
        
        if(gResolverManager->msgQue)
        {
            DELETE(msgque_obj, gResolverManager->msgQue);
        }

        pthread_resource_lock_delete(gResolverManager->glock);
        while(gResolverManager->head != NULL)
        {
            node = gResolverManager->head->next;
            free(gResolverManager->head);
            gResolverManager->head = node;
        }

        close(gResolverManager->mSockfd);
        
        free(gResolverManager);
        gResolverManager = NULL;
    }
}

int property_get(char * property_name, char * property_val, char * default_val)
{
    if(property_name == NULL || property_val == NULL || default_val == NULL)
    {
        DPrint("param error, please check\n");
        return PARAM_ERR;
    }

    int iret = PARAM_ERR;
    int fd = property_socket_init();
    if(fd < NO_ERR)
    {
        return fd;
    }
    property_data_s propertyVal;
    memset(&propertyVal, 0x0, sizeof(propertyVal));
    propertyVal.action = GET_ACTION;
    strcpy(propertyVal.property_name, property_name);

    iret = TEMP_FAILURE_RETRY(send(fd, &propertyVal, sizeof(property_data_s), 0));
    if(iret < 0)
    {
        close(fd);
        strcpy(property_val, default_val);
        return SOCKET_SEND_ERR;
    }

    memset(&propertyVal, 0x0, sizeof(property_data_s));
    iret = TEMP_FAILURE_RETRY(recv(fd, &propertyVal, sizeof(property_data_s), 0));
    if(iret < 0)
    {
        close(fd);
        strcpy(property_val, default_val);
        return SOCKET_SEND_ERR;
    }

    strcpy(property_val, propertyVal.property_val);
    close(fd);

    return NO_ERR;
}

int property_set(char * property_name, char * property_val)
{
    if(property_name == NULL || property_val == NULL)
    {
        DPrint("param error, please check\n");
        return PARAM_ERR;
    }

    int iret = PARAM_ERR;
    int fd = property_socket_init();
    if(fd < NO_ERR)
    {
        return fd;
    }
    property_data_s propertyVal;
    propertyVal.action = SET_ACTION;
    strcpy(propertyVal.property_name, property_name);
    strcpy(propertyVal.property_val, property_val);
    strcpy(propertyVal.default_val, property_val);

    iret = TEMP_FAILURE_RETRY(send(fd, &propertyVal, sizeof(property_data_s), 0));
    if(iret < 0)
    {
        close(fd);
        return SOCKET_SEND_ERR;
    }

    close(fd);

    return NO_ERR;
}

#ifdef PROPERTY_UNIT_TEST

struct property_observer_sub {
    struct property_observer_t gBaseObserver;
    char version;
};

static int onPropertyValChangeSub(struct property_observer_t * pThis, char * property_name, char * property_val)
{
    struct property_observer_sub * own = (struct property_observer_sub *)pThis;
    
    IPrint("version:%d, property_name:%s, property_val:%s\n", own->version, property_name, property_val);

    return NO_ERR;
}

int property_unit_test_main(int argc, char * argv[])
{
    unsigned sleepCnt = NO_ERR;
    PropertyResolver * IResolver = getPropertyResolver();
    if(IResolver == NULL)
    {
        DPrint("getPropertyResolver failed\n");
        return PARAM_ERR;
    }

    struct property_observer_sub observer;
    observer.version = 1;
    observer.gBaseObserver.onPropertyValChange = onPropertyValChangeSub;

    IResolver->RegisterObserver(IResolver, "sys.test", (struct property_observer_t *)&observer);

    int result = NO_ERR;
    char property_val[VALUE_MAX] = {0};
    while(1)
    {
        sleep(1);
        if(sleepCnt > 60)
        {
            break;
        }

        if(sleepCnt == 4)
        {
            result = property_set("sys.test", "1113");
            DPrint("===>%d,%d<===\n", sleepCnt, result);
        }
        else if(sleepCnt == 2)
        {
            result = property_get("sys.test", property_val, "false");
            DPrint("===>%d,%d<===property_val:%s\n", sleepCnt, result, property_val);
        }
        else if(sleepCnt == 5)
        {
            result = property_get("sys.test", property_val, "false");
            DPrint("===>%d,%d<===property_val:%s\n", sleepCnt, result, property_val);
        }

        sleepCnt++;
    }

    IResolver->UnRegisterObserver(IResolver, "sys.test", (struct property_observer_t *)&observer);
    releasePropertyResolver();

    return NO_ERR;
}
#endif