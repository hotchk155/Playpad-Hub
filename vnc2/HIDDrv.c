#include "playpad.h"

#define MOUSE_MAX 1023
#define MOUSE_DEFAULT (MOUSE_MAX/2)

#define MOUSE_BUTTONL_NOTE (byte)60
#define MOUSE_BUTTONR_NOTE (byte)61
#define MOUSE_BUTTONM_NOTE (byte)62
#define MOUSE_X_CC (byte)71
#define MOUSE_Y_CC (byte)72

//////////////////////////////////////////////////////////////////////
//
//     HH  HH  IIII  DDDDD
//    HH  HH   II   DD  DD
//   HHHHHH   II   DD  DD
//  HH  HH   II   DD  DD
// HH  HH  IIII  DDDDD
//
//	
void run_hid_host(VOS_HANDLE usb_handle)
{
	int mouseX;
	int mouseY;
	char mouseButtons;
	char mask;

	char cc;
	char ccX;
	char ccY;
	
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
	
	vos_init_semaphore(&read_completion_event, 0);
	
	// Look for a HID keyboard device
	device_class.dev_class = USB_CLASS_HID;
	device_class.dev_subclass = USB_SUBCLASS_HID_BOOT_INTERFACE;
	device_class.dev_protocol = USB_PROTOCOL_HID_KEYBOARD;
	usbhost_cmd.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_FIND_HANDLE_BY_CLASS;
	usbhost_cmd.handle.dif = NULL;
	usbhost_cmd.set = &device_class;
	usbhost_cmd.get = &interface_handle;
	status = vos_dev_ioctl(usb_handle, &usbhost_cmd);
	if(USBHOST_OK != status) {
		
		// Look for a HID mouse device		
		device_class.dev_class = USB_CLASS_HID;
		device_class.dev_subclass = USB_SUBCLASS_HID_BOOT_INTERFACE;
		device_class.dev_protocol = USB_PROTOCOL_HID_MOUSE;		
		usbhost_cmd.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_FIND_HANDLE_BY_CLASS;
		usbhost_cmd.handle.dif = NULL;
		usbhost_cmd.set = &device_class;
		usbhost_cmd.get = &interface_handle;
		status = vos_dev_ioctl(usb_handle, &usbhost_cmd);
		if(USBHOST_OK != status) {
			return;
		}
	}
				
	usbhost_cmd.ioctl_code = VOS_IOCTL_USBHOST_DEVICE_GET_INT_IN_ENDPOINT_HANDLE;
	usbhost_cmd.handle.dif = interface_handle;
	usbhost_cmd.get = &int_endpoint;
	usbhost_cmd.set = NULL;
	status = vos_dev_ioctl(usb_handle, &usbhost_cmd);

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
	vos_dev_ioctl(usb_handle, &usbhost_cmd);
	

	mouseX = MOUSE_DEFAULT;
	mouseY = MOUSE_DEFAULT;
	mouseButtons = 0;

	set_porta_led(1);
	
	while (1)
	{
		vos_memset(&transfer_block, 0, sizeof(transfer_block));
		transfer_block.s = &read_completion_event;
		transfer_block.ep = int_endpoint;
		transfer_block.buf = usb_rx_data;
		transfer_block.len = endpoint_descriptor.max_size;
		transfer_block.flags = 0;

		if (vos_dev_read(usb_handle, (byte*)&transfer_block, sizeof(transfer_block), NULL) != USBHOST_OK) {
			break;
		}
		
		switch(device_class.dev_protocol)
		{
			/* ---------------------------------------------------------------------------------------
					Byte	D7	D6	D5	D4	D3	D2	D1	D0	Comment
					0		-	-	-	-	-	M	R	L	buttons
					1		X7	X6	X5	X4	X3	X2	X1	X0	X data byte
					2		Y7	Y6	Y5	Y4	Y3	Y2	Y1	Y0	Y data bytes
			*/
			case USB_PROTOCOL_HID_MOUSE:
				mask = mouseButtons	^ usb_rx_data[0];
				if(mask & 0x01) {
					send_output_midi(0x90, MOUSE_BUTTONL_NOTE, (usb_rx_data[0] & 0x01)? 127: 0);					
				}
				if(mask & 0x02) {
					send_output_midi(0x90, MOUSE_BUTTONR_NOTE, (usb_rx_data[0] & 0x02)? 127: 0);					
				}
				if(mask & 0x04) {
					send_output_midi(0x90, MOUSE_BUTTONM_NOTE, (usb_rx_data[0] & 0x04)? 127: 0);					
				}
				mouseButtons = usb_rx_data[0];
				
				/////////////////////////////
				mouseX += (char)usb_rx_data[1];				
				if(mouseX < 0) {
					mouseX = 0;
				} else 
				if(mouseX > MOUSE_MAX) {
					mouseX = MOUSE_MAX;
				}			
				cc=mouseX>>3;
				if(cc != ccX) {
					ccX = cc;				
					send_output_midi(0xB0, MOUSE_X_CC, ccX);
				}			
				
				/////////////////////////////
				mouseY += (char)usb_rx_data[2];				
				if(mouseY < 0) {
					mouseY = 0;
				} else 
				if(mouseY > MOUSE_MAX) {
					mouseY = MOUSE_MAX;
				}			
				cc=mouseY>>3;
				if(cc != ccY) {
					ccY = cc;				
					send_output_midi(0xB0, MOUSE_Y_CC, ccY);
				}			
				break;
			case USB_PROTOCOL_HID_KEYBOARD:
				break;
		}
	}
	set_porta_led(0);
	
}
