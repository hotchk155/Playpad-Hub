#include "string.h"
#include "errno.h"
#include "timers.h"
#include "stdlib.h"
#include "vos.h"
#include "ioctl.h"
#include "SPISlave.h"
#include "GPIO.h"
#include "UART.h"
#include "USB.h"
#include "USBHost.h"
#include "USBHID.h"
#include "USBHostHID.h"
#include "USBHostGenericDrv.h"

typedef unsigned char byte;

enum {
	VOS_DEV_GPIO_A,
	VOS_DEV_UART,
	VOS_DEV_SPISLAVE,
	VOS_DEV_USBHOST,
	VOS_DEV_USBHOSTGENERIC,
	VOS_NUMBER_DEVICES	   
};

#define SZ_USB_RX_DATA 64
extern byte usb_rx_data[SZ_USB_RX_DATA];
void run_hid_host(VOS_HANDLE usb_handle);
void run_launchpad_host(VOS_HANDLE usb_handle);
void run_midi_class_host(VOS_HANDLE usb_handle);
void send_output(byte *data, int data_size);
void send_output_midi(byte b0, byte b1, byte b2);
void set_porta_led(byte val);

