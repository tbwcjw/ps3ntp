#ifndef __VSHTASK_H__
#define __VSHTASK_H__

#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif

	extern int32_t vshtask_A02D46E7(int32_t arg, const char *msg);					// vshtask_notification_msg()
#ifdef __cplusplus
}
#endif

#define vshtask_notify(msg) vshtask_A02D46E7(0, msg)

#endif
