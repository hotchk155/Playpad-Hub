//////////////////////////////////////////////////////////////////////
// 

//////////////////////////////////////////////////////////////////////

//
// INCLUDE FILES
//
#include "playpad.h"


	
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


byte usb_rx_data[SZ_USB_RX_DATA];

byte gpio_status;
	
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
// 
//
//////////////////////////////////////////////////////////////////////
void set_porta_led(byte val) 
{
	if(val) {
		gpio_status |= LED_USB_A; 
	}
	else {
		gpio_status &= ~LED_USB_A; 
	}
	vos_dev_write(gpio_handle,&gpio_status,1,NULL);
}

//////////////////////////////////////////////////////////////////////
//
// 
//
//////////////////////////////////////////////////////////////////////
void send_output(byte *data, int data_size) 
{
	//vos_dev_write(uart_handle, data, data_size, NULL);				
	vos_dev_write(spi_handle, data, data_size, NULL);				
}

//////////////////////////////////////////////////////////////////////
//
// 
//
//////////////////////////////////////////////////////////////////////
void send_output_midi(byte b0, byte b1, byte b2) 
{
	byte msg[3];
	msg[0] = b0;
	msg[1] = b1;
	msg[2] = b2;
	//vos_dev_write(uart_handle, &msg[0], 3, NULL);				
	vos_dev_write(spi_handle, &msg[0], 3, NULL);				
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
	SET_UART_BAUDRATE(VOS_IOCTL_UART_SET_BAUD_RATE, 38400);
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
			run_hid_host(usb_handle);
		//}
	}
}	

	
