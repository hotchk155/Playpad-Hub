//////////////////////////////////////////////////////////////////////
// 

//////////////////////////////////////////////////////////////////////

//
// INCLUDE FILES
//

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
	
//
// VARIABLE DECL
//
vos_tcb_t *setup_thread;
vos_tcb_t *usb_thread;
vos_tcb_t *spi_thread;

vos_semaphore_t setup_complete_event;

VOS_HANDLE gpio_handle;
VOS_HANDLE spi_handle;
VOS_HANDLE uart_handle;
VOS_HANDLE usb_handle;
VOS_HANDLE usb_function_handle;

#define SZ_USB_RX_DATA 64
byte usb_rx_data[SZ_USB_RX_DATA];

byte gpio_status;
#define SET_GPIO_STATUS(s) gpio_status |= (s); vos_dev_write(gpio_handle,&gpio_status,1,NULL)
#define CLEAR_GPIO_STATUS(s) gpio_status &= ~(s); vos_dev_write(gpio_handle,&gpio_status,1,NULL)

#define SET_SPI_OPTION(c, p) \
	spi_cmd.ioctl_code = (c); \
	spi_cmd.set.param = (p); \
	vos_dev_ioctl(spi_handle, &spi_cmd);
	
#define SET_UART_OPTION(c, p) \
	uart_cmd.ioctl_code = (c); \
	uart_cmd.set.param = (p); \
	vos_dev_ioctl(uart_handle, &uart_cmd);

#define SET_UART_BAUDRATE(c, p) \
	uart_cmd.ioctl_code = (c); \
	uart_cmd.set.uart_baud_rate = (p); \
	vos_dev_ioctl(uart_handle, &uart_cmd);

#define SET_GPIO_OPTION(c, p) \
	gpio_cmd.ioctl_code = (c); \
	gpio_cmd.value = (p); \
	vos_dev_ioctl(gpio_handle, &gpio_cmd);


#define LED_USB_A 0x02
#define LED_USB_B 0x04
#define LED_SIGNAL 0x08

//
// FUNCTION PROTOTYPES
//
void setup();
void run_spi_to_usb();
void run_usb_to_spi();

	


//////////////////////////////////////////////////////////////////////
//
// IOMUX SETUP
// ALWAYS VNC2 32PIN PACKAGE
// 
// 11	IO0		DEBUG		debug_if
// 12	IO1		GPIO		gpio[A1]	LED
// 14	IO2		GPIO		gpio[A2]	LED
// 15	IO3		GPIO		gpio[A3]	INT
// 23	IO4		UART		uart_txd
// 24	IO5		UART		uart_rxd
// 25	IO6		UART		uart_rts#
// 26	IO7		UART		uart_cts#
// 29	IO8		SPI			spi_s0_clk
// 30	IO9		SPI			spi_s0_mosi
// 31	IO10	SPI			spi_s0_miso
// 32	IO11	SPI			spi_s0_ss#
//
//////////////////////////////////////////////////////////////////////
void iomux_setup()
{
	vos_iomux_define_bidi( 199, 	IOMUX_IN_DEBUGGER, IOMUX_OUT_DEBUGGER);
	
	// GPIOS
	vos_iomux_define_output(12, 	IOMUX_OUT_GPIO_PORT_A_1);
	vos_iomux_define_output(14, 	IOMUX_OUT_GPIO_PORT_A_2);
	vos_iomux_define_output(15, 	IOMUX_OUT_GPIO_PORT_A_3);
	
	// UART
	vos_iomux_define_output(23, 	IOMUX_OUT_UART_TXD);
	vos_iomux_define_input(24, 		IOMUX_IN_UART_RXD);
	vos_iomux_define_output(25, 	IOMUX_OUT_UART_RTS_N);
	vos_iomux_define_input(26, 		IOMUX_IN_UART_CTS_N);
	
	// SPI SLAVE 0
	vos_iomux_define_input(	29, 	IOMUX_IN_SPI_SLAVE_0_CLK);
	vos_iomux_define_input(	30, 	IOMUX_IN_SPI_SLAVE_0_MOSI);
	vos_iomux_define_output(31, 	IOMUX_OUT_SPI_SLAVE_0_MISO);
	vos_iomux_define_input(	32, 	IOMUX_IN_SPI_SLAVE_0_CS);
}

//////////////////////////////////////////////////////////////////////
//
// MAIN
//
//////////////////////////////////////////////////////////////////////
void main(void)
{
	usbhost_context_t usb_ctx;
	gpio_context_t gpio_ctx;
	spislave_context_t spi_ctx;
	uart_context_t uart_ctx;

	// Kernel initialisation
	vos_init(50, VOS_TICK_INTERVAL, VOS_NUMBER_DEVICES);
	vos_set_clock_frequency(VOS_48MHZ_CLOCK_FREQUENCY);
	vos_set_idle_thread_tcb_size(512);

	// Set up the io multiplexing
	iomux_setup();

	spi_ctx.slavenumber = SPI_SLAVE_0;
	spi_ctx.buffer_size = 64;
	spislave_init(VOS_DEV_SPISLAVE, &spi_ctx);
	
	// Initialise GPIO port A
	gpio_ctx.port_identifier = GPIO_PORT_A;
	gpio_init(VOS_DEV_GPIO_A,&gpio_ctx); 
	
	// Initialise UART
	uart_ctx.buffer_size = 64;
	uart_init(VOS_DEV_UART,&uart_ctx); 

	// Initialise USB Host 
	usb_ctx.if_count = 8;
	usb_ctx.ep_count = 16;
	usb_ctx.xfer_count = 2;
	usb_ctx.iso_xfer_count = 2;
	usbhost_init(VOS_DEV_USBHOST, -1, &usb_ctx);
	
	// Initialise the USB function device
	usbhostGeneric_init(VOS_DEV_USBHOSTGENERIC);
	
	vos_init_semaphore(&setup_complete_event,0);
	
	setup_thread = vos_create_thread_ex(10, 1024, setup, "setup", 0);
	usb_thread = vos_create_thread_ex(20, 4096, run_usb_to_spi, "run_usb_to_spi", 0);
//	spi_thread = vos_create_thread_ex(20, 1024, run_spi_to_usb, "run_spi_to_usb", 0);
	
	// And start the thread
	vos_start_scheduler();

main_loop:
	goto main_loop;
}
	
//////////////////////////////////////////////////////////////////////
//

// APPLICATION SETUP THREAD FUNCTION
//
//////////////////////////////////////////////////////////////////////
void setup()
{	
	gpio_ioctl_cb_t 		gpio_cmd;
	common_ioctl_cb_t 		uart_cmd;
	common_ioctl_cb_t 		spi_cmd;
	usbhost_ioctl_cb_t 		usb_cmd;
	
	// open device handles
	spi_handle = vos_dev_open(VOS_DEV_SPISLAVE);
	gpio_handle = vos_dev_open(VOS_DEV_GPIO_A);	
	uart_handle = vos_dev_open(VOS_DEV_UART);	
	usb_handle = vos_dev_open(VOS_DEV_USBHOST);
	usb_function_handle = NULL;
	
	// initialise GPIO
	SET_GPIO_OPTION(VOS_IOCTL_GPIO_SET_MASK, LED_USB_A|LED_USB_B|LED_SIGNAL)
	
	// initialise SPI slave mode
	SET_SPI_OPTION(VOS_IOCTL_SPI_SLAVE_SCK_CPHA, SPI_SLAVE_SCK_CPHA_0);
	SET_SPI_OPTION(VOS_IOCTL_SPI_SLAVE_SCK_CPOL, SPI_SLAVE_SCK_CPOL_0);
	SET_SPI_OPTION(VOS_IOCTL_SPI_SLAVE_DATA_ORDER, SPI_SLAVE_DATA_ORDER_MSB);
	SET_SPI_OPTION(VOS_IOCTL_SPI_SLAVE_SET_MODE, SPI_SLAVE_MODE_FULL_DUPLEX);
	SET_SPI_OPTION(VOS_IOCTL_SPI_SLAVE_SET_ADDRESS, 0);
	SET_SPI_OPTION(VOS_IOCTL_COMMON_ENABLE_DMA, DMA_ACQUIRE_AND_RETAIN);
	
	// UART
	SET_UART_BAUDRATE(VOS_IOCTL_UART_SET_BAUD_RATE, 9600);
	SET_UART_OPTION(VOS_IOCTL_UART_SET_FLOW_CONTROL, UART_FLOW_NONE);
	SET_UART_OPTION(VOS_IOCTL_UART_SET_DATA_BITS, UART_DATA_BITS_8);
	SET_UART_OPTION(VOS_IOCTL_UART_SET_STOP_BITS, UART_STOP_BITS_1);
	SET_UART_OPTION(VOS_IOCTL_UART_SET_PARITY, UART_PARITY_NONE);
		
		
	usb_cmd.ioctl_code = VOS_IOCTL_USBHOST_SET_HANDLE_MODE_EXTENDED;
	vos_dev_ioctl(usb_handle, &usb_cmd);
	
	// Release other application threads
	vos_signal_semaphore(&setup_complete_event);
}
	
//////////////////////////////////////////////////////////////////////
void run_usb_generic_host_to_spi(VOS_HANDLE usb_handle) {

	usbhost_ioctl_cb_t 			usb_cmd;
	usbhost_ioctl_cb_vid_pid_t 	vid_pid;

	unsigned short usb_rx_len;
	usbhost_device_handle_ex usb_device_handle;
	usbhostGeneric_ioctl_t generic_function_cmd;
	usbhostGeneric_ioctl_cb_attach_t generic_function_attach;	
	VOS_HANDLE handle;

	// Find the first USB device
	usb_cmd.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_GET_NEXT_HANDLE;
	usb_cmd.handle.dif = NULL;
	usb_cmd.set = NULL;
	usb_cmd.get = &usb_device_handle;			
	if (vos_dev_ioctl(usb_handle, &usb_cmd) == USBHOST_OK)
	{
		// query the device VID/PID
		usb_cmd.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_GET_VID_PID;
		usb_cmd.handle.dif = usb_device_handle;
		usb_cmd.get = &vid_pid;

	
		// Load the function driver
		handle = vos_dev_open(VOS_DEV_USBHOSTGENERIC);
		
		// Attach the function driver to the base driver
		generic_function_attach.hc_handle = usb_handle;
		generic_function_attach.ifDev = usb_device_handle;
		generic_function_cmd.ioctl_code = VOS_IOCTL_USBHOSTGENERIC_ATTACH;
		generic_function_cmd.set.att = &generic_function_attach;
		if (vos_dev_ioctl(handle, &generic_function_cmd) == USBHOSTGENERIC_OK)
		{					
			// Turn on the LED for this port
			SET_GPIO_STATUS(LED_USB_A);

			// flag that the port is attached
			VOS_ENTER_CRITICAL_SECTION;
			usb_function_handle = handle;
			VOS_EXIT_CRITICAL_SECTION;
								
			// now we loop until the launchpad is detached
			while(1)
			{						
				// listen for data from launchpad
				unsigned short result = vos_dev_read(handle, usb_rx_data, sizeof usb_rx_data, &usb_rx_len);
				if(0 != result) {
					// break when the launchpad is detached				
					break; 
				}								
				vos_dev_write(spi_handle, usb_rx_data, usb_rx_len, NULL);
			}
			
			// flag that the port is no longer attached
			VOS_ENTER_CRITICAL_SECTION;
			usb_function_handle = NULL;
			VOS_EXIT_CRITICAL_SECTION;
			
			// turn off the activity LED
			CLEAR_GPIO_STATUS(LED_USB_A);
		}
		
		// close the function driver
		vos_dev_close(handle);
	}
}

//////////////////////////////////////////////////////////////////////
//
//     MM    MM  IIII  DDDDD   IIII
//    MMM  MMM   II   DD  DD   II
//   MM MM MM   II   DD  DD   II
//  MM    MM   II   DD  DD   II
// MM    MM  IIII  DDDDD   IIII
//
void run_usb_midi_to_spi(VOS_HANDLE usb_handle) 
{
	unsigned char state;
	int status;
	usbhost_device_handle_ex interface_handle;
	usbhost_ep_handle_ex tx_endpoint;
	usbhost_ep_handle_ex rx_endpoint;
	usbhost_ep_handle_ex ctrl_endpoint;
	vos_semaphore_t read_completion_event;
	usbhost_ioctl_cb_t usbhost_cmd;
	usbhost_ioctl_cb_class_t device_class;
	usbhost_ioctl_cb_vid_pid_t vid_pid;
	
	usbhost_xfer_t transfer_block;
	usb_deviceRequest_t device_request;
	usbhost_ioctl_cb_ep_info_t endpoint_descriptor;
	
	vos_init_semaphore(&read_completion_event, 0);
	
	device_class.dev_class = USB_CLASS_AUDIO;
	device_class.dev_subclass = USB_SUBCLASS_AUDIO_MIDISTREAMING;
	device_class.dev_protocol = USB_PROTOCOL_ANY;

	usbhost_cmd.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_FIND_HANDLE_BY_CLASS;
	usbhost_cmd.handle.dif = NULL;
	usbhost_cmd.set = &device_class;
	usbhost_cmd.get = &interface_handle;
	status = vos_dev_ioctl(usb_handle, &usbhost_cmd);
	if(USBHOST_OK != status) {
		return;
	}
		

	usbhost_cmd.ioctl_code = VOS_IOCTL_USBHOST_GET_USB_STATE;
	usbhost_cmd.handle.dif = interface_handle;
	usbhost_cmd.get = &state;
	usbhost_cmd.set = NULL;
	status = vos_dev_ioctl(usb_handle, &usbhost_cmd);
	if(USBHOST_OK != status) {
		return;
	}

	usbhost_cmd.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_GET_VID_PID;
	usbhost_cmd.handle.dif = interface_handle;
	usbhost_cmd.get = &vid_pid;
	usbhost_cmd.set = NULL;
	status = vos_dev_ioctl(usb_handle, &usbhost_cmd);
	if(USBHOST_OK != status) {
		return;
	}
	
	usbhost_cmd.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_GET_BULK_OUT_ENDPOINT_HANDLE;
	usbhost_cmd.handle.dif = interface_handle;
	usbhost_cmd.get = &tx_endpoint;
	usbhost_cmd.set = NULL;
	status = vos_dev_ioctl(usb_handle, &usbhost_cmd);
	if(USBHOST_OK != status) {
		return;
	}

	usbhost_cmd.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_GET_BULK_IN_ENDPOINT_HANDLE;
	usbhost_cmd.handle.dif = interface_handle;
	usbhost_cmd.get = &rx_endpoint;
	usbhost_cmd.set = NULL;
	status = vos_dev_ioctl(usb_handle, &usbhost_cmd);
	if(USBHOST_OK != status) {
		return;
	}

	usbhost_cmd.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_GET_CONTROL_ENDPOINT_HANDLE;
	usbhost_cmd.handle.dif = interface_handle;
	usbhost_cmd.get = &ctrl_endpoint;
	usbhost_cmd.set = NULL;
	status = vos_dev_ioctl(usb_handle, &usbhost_cmd);
	if(USBHOST_OK != status) {
		return;
	}

	// send a SetIdle to the device
	device_request.bmRequestType = USB_BMREQUESTTYPE_HOST_TO_DEV |
		USB_BMREQUESTTYPE_CLASS |
		USB_BMREQUESTTYPE_INTERFACE;
	device_request.bRequest = 0x0a;
	device_request.wValue = 0;
	device_request.wIndex = 0;
	device_request.wLength = 0;

	usbhost_cmd.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_SETUP_TRANSFER;
	usbhost_cmd.handle.ep = ctrl_endpoint;
	usbhost_cmd.set = &device_request;
	status = vos_dev_ioctl(usb_handle, &usbhost_cmd);

	while (1)
	{
		vos_memset(&transfer_block, 0, sizeof(transfer_block));
		transfer_block.cond_code = USBHOST_CC_NOTACCESSED;
		transfer_block.flags = USBHOST_XFER_FLAG_START_BULK_ENDPOINT_LIST|USBHOST_XFER_FLAG_ROUNDING;
		transfer_block.s = &read_completion_event;
		transfer_block.ep = rx_endpoint;
		transfer_block.buf = usb_rx_data;
		transfer_block.len = SZ_USB_RX_DATA;

		status =  vos_dev_read(usb_handle, (byte*)&transfer_block, sizeof(transfer_block), NULL);
		if(status != USBHOST_OK) {
			break;
		}
		vos_dev_write(uart_handle, (byte*)usb_rx_data, transfer_block.len, NULL);				
	}
}

//////////////////////////////////////////////////////////////////////
//
//     HH  HH  IIII  DDDDD
//    HH  HH   II   DD  DD
//   HHHHHH   II   DD  DD
//  HH  HH   II   DD  DD
// HH  HH  IIII  DDDDD
//
//	
void run_usb_hid_to_spi(VOS_HANDLE usb_handle) 
{
	int status;
	usbhost_device_handle_ex interface_handle;
	usbhost_ep_handle_ex int_endpoint;
	usbhost_ep_handle_ex ctrl_endpoint;
	vos_semaphore_t read_completion_event;
	usbhost_ioctl_cb_t usbhost_cmd;
	usbhost_ioctl_cb_class_t device_class;
	
	usbhost_xfer_t transfer_block;
	usb_deviceRequest_t device_request;
	usbhost_ioctl_cb_ep_info_t endpoint_descriptor;
	byte *data_buffer;
	
	vos_init_semaphore(&read_completion_event, 0);
	
	device_class.dev_class = USB_CLASS_HID;
	device_class.dev_subclass = USB_SUBCLASS_ANY;
	device_class.dev_protocol = USB_PROTOCOL_ANY;

	usbhost_cmd.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_FIND_HANDLE_BY_CLASS;
	usbhost_cmd.handle.dif = NULL;
	usbhost_cmd.set = &device_class;
	usbhost_cmd.get = &interface_handle;
	status = vos_dev_ioctl(usb_handle, &usbhost_cmd);
	if(USBHOST_OK != status) {
		return;
	}

	usbhost_cmd.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_GET_INT_IN_ENDPOINT_HANDLE;
	usbhost_cmd.handle.dif = interface_handle;
	usbhost_cmd.get = &int_endpoint;
	usbhost_cmd.set = NULL;
	status = vos_dev_ioctl(usb_handle, &usbhost_cmd);
	if(USBHOST_OK != status) {
		return;
	}

	usbhost_cmd.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_GET_CONTROL_ENDPOINT_HANDLE;
	usbhost_cmd.handle.dif = interface_handle;
	usbhost_cmd.get = &ctrl_endpoint;
	usbhost_cmd.set = NULL;
	status = vos_dev_ioctl(usb_handle, &usbhost_cmd);
	if(USBHOST_OK != status) {
		return;
	}
	
	usbhost_cmd.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_GET_ENDPOINT_INFO;
	usbhost_cmd.handle.ep = int_endpoint;
	usbhost_cmd.get = &endpoint_descriptor;
	usbhost_cmd.set = NULL;
	status = vos_dev_ioctl(usb_handle, &usbhost_cmd);
	if(USBHOST_OK != status) {
		return;
	}

	data_buffer = (byte*)malloc((size_t)endpoint_descriptor.max_size);
	if(!data_buffer) {
		return;
	}

// send a SetIdle to the device
		device_request.bmRequestType = USB_BMREQUESTTYPE_HOST_TO_DEV |
			USB_BMREQUESTTYPE_CLASS |
			USB_BMREQUESTTYPE_INTERFACE;
		device_request.bRequest = 0x0a;
		device_request.wValue = 0;
		device_request.wIndex = 0;
		device_request.wLength = 0;

		usbhost_cmd.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_SETUP_TRANSFER;
		usbhost_cmd.handle.ep = ctrl_endpoint;
		usbhost_cmd.set = &device_request;

	status = vos_dev_ioctl(usb_handle, &usbhost_cmd);
	while (1)
	{
		vos_memset(&transfer_block, 0, sizeof(transfer_block));
		transfer_block.s = &read_completion_event;
		transfer_block.ep = int_endpoint;
		transfer_block.buf = data_buffer;
		transfer_block.len = endpoint_descriptor.max_size;
		transfer_block.flags = 0;

		if (vos_dev_read(usb_handle, (byte*)&transfer_block, sizeof(transfer_block), NULL) != USBHOST_OK) {
			break;
		}

		vos_dev_write(uart_handle, (byte*)data_buffer, transfer_block.len, NULL);				
	}

	free(data_buffer);
}

//////////////////////////////////////////////////////////////////////
//
// Get connect state
//
//////////////////////////////////////////////////////////////////////
byte get_usb_host_connect_state(VOS_HANDLE usb_handle) {
	usbhost_ioctl_cb_t usb_cmd;
	byte connect_state = PORT_STATE_DISCONNECTED;
	if (usb_handle) {
		usb_cmd.ioctl_code = VOS_IOCTL_USBHOST_GET_CONNECT_STATE;
		usb_cmd.get = &connect_state;
		vos_dev_ioctl(usb_handle, &usb_cmd);
		if (connect_state == PORT_STATE_CONNECTED)
		{
			// repeat if connected to see if we move to enumerated
			vos_dev_ioctl(usb_handle, &usb_cmd);
		}		
	}
	return connect_state;
}
	
//////////////////////////////////////////////////////////////////////
//
// RUN USB HOST PORT
//
//////////////////////////////////////////////////////////////////////
void run_usb_to_spi()
{
	// wait for setup to complete
	vos_wait_semaphore(&setup_complete_event);
	vos_signal_semaphore(&setup_complete_event);
	
	
	// loop forever
	while(1)
	{
		vos_delay_msecs(500);
		// is the device enumerated on this port?
		//if (get_usb_host_connect_state(usb_handle) == PORT_STATE_ENUMERATED)
		//{
			//run_usb_generic_host_to_spi(usb_handle);
		//}
		//else
		//{
		//run_usb_hid_to_spi(usb_handle);
			run_usb_midi_to_spi(usb_handle);
		//}
	}
}	

	
