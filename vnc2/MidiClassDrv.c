#include "playpad.h"

//////////////////////////////////////////////////////////////////////
//
//     MM    MM  IIII  DDDDD   IIII
//    MMM  MMM   II   DD  DD   II
//   MM MM MM   II   DD  DD   II
//  MM    MM   II   DD  DD   II
// MM    MM  IIII  DDDDD   IIII
//
void run_midi_class_host(VOS_HANDLE usb_handle) 
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
		send_output(usb_rx_data, transfer_block.len);		
	}
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
