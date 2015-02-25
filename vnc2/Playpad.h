#ifndef _PLAYPAD_H_
#define _PLAYPAD_H_

#include "vos.h"


#include "USBHost.h"
#include "ioctl.h"
#include "SPISlave.h"
#include "GPIO.h"
#include "UART.h"
#include "string.h"
#include "errno.h"
#include "timers.h"
#include "stdlib.h"

typedef unsigned char byte;

#define VOS_DEV_GPIO_A		   		0
#define VOS_DEV_UART	   			1
#define VOS_DEV_SPISLAVE	   		2
#define VOS_DEV_USBHOST_1	  		3
#define VOS_DEV_USBHOST_2	   		4
#define VOS_DEV_USBHOSTGENERIC_1   	5
#define VOS_DEV_USBHOSTGENERIC_2   	6
#define VOS_NUMBER_DEVICES	   		7

#endif // _PLAYPAD_H_

