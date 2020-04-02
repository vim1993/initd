#ifndef __PROPERTY_SERVICE_H__
#define __PROPERTY_SERVICE_H__

#include "common.h"

#define PROPERTY_SERVICE_ENV "PROPERTY_SERVICE"

#ifdef __cplusplus
extern "C" {
#endif

#define NAME_MAX 32
#define VALUE_MAX 92

typedef enum PropertyAction {
    GET_ACTION,
    SET_ACTION,
    OBR_ACTION,
    OBS_ACTION
}PropertyAction_e;

typedef struct property_data_s
{
    PropertyAction_e action;
    char property_name[NAME_MAX];
    char property_val[VALUE_MAX];
    char default_val[VALUE_MAX];
}property_data_s;


extern int property_get(char * property_name, char * property_val, char * default_val);
extern int property_set(char * property_name, char * property_val);

typedef struct property_observer_t
{
    int (*onPropertyValChange)(struct property_observer_t * pThis, char * property_name, char * property_val);
}property_observer_t;

extern int register_property_observer(property_observer_t * observer);

typedef struct PropertyResolver {
    int (*RegisterObserver)(struct PropertyResolver * pThis, char * property_name, struct property_observer_t * observer);
    int (*UnRegisterObserver)(struct PropertyResolver * pThis, char * property_name, struct property_observer_t * observer);
}PropertyResolver;

//for PropertyResolver instance
extern PropertyResolver * getPropertyResolver();

//for release Resolver instance
extern void releasePropertyResolver();

#ifdef PROPERTY_UNIT_TEST
extern int property_unit_test_main(int argc, char * argv[]);
#endif

#ifdef __cplusplus
}
#endif

#endif // !__PROPERTY_SERVICE_H__