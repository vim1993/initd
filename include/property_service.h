#ifndef __PROPERTY_SERVICE_H__
#define __PROPERTY_SERVICE_H__

#ifdef __cplusplus
extern "C" {
#endif

extern I32 property_get(I8 * property_name, I8 * property_val, I8 * default_val);
extern I32 property_set(I8 * property_name, I8 * property_val);

extern I32 property_start(void);

#ifdef __cplusplus
}
#endif

#endif // !__PROPERTY_SERVICE_H__