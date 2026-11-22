#ifndef __VSHMAIN_H__
#define __VSHMAIN_H__

#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif
	//0 = xmb, 1 = game, 2 = video player, 3 = emu
	extern uint32_t vshmain_EB757101(void);					//GetCooperationMode()
#ifdef __cplusplus
}
#endif

#define GetCooperationMode vshmain_EB757101

#endif
