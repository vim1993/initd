#ifndef __PROPERTY_SERVICE_H__
#define __PROPERTY_SERVICE_H__

#ifdef __cplusplus
extern "C" {
#endif

extern I32 property_service_init(void);
extern I32 property_service_handler(int epollfd, int sockfd);
extern I32 property_service_uninit(void);

#ifdef __cplusplus
}
#endif

#endif // !__PROPERTY_SERVICE_H__