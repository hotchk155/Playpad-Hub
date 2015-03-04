#include "playpad.h"

//////////////////////////////////////////////////////////////////////
void run_launchpad_host(VOS_HANDLE usb_handle) {

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
			set_porta_led(1);

			// flag that the port is attached
//			VOS_ENTER_CRITICAL_SECTION;
//			usb_function_handle = handle;
//			VOS_EXIT_CRITICAL_SECTION;
								
			// now we loop until the launchpad is detached
			while(1)
			{						
				// listen for data from launchpad
				unsigned short result = vos_dev_read(handle, usb_rx_data, sizeof usb_rx_data, &usb_rx_len);
				if(0 != result) {
					// break when the launchpad is detached				
					break; 
				}								
				send_output(usb_rx_data, usb_rx_len);						
			}
			
			// flag that the port is no longer attached
//			VOS_ENTER_CRITICAL_SECTION;
//			usb_function_handle = NULL;
//			VOS_EXIT_CRITICAL_SECTION;
			
			// turn off the activity LED
			set_porta_led(0);
		}
		
		// close the function driver
		vos_dev_close(handle);
	}
}

