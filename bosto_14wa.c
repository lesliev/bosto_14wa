/*
 *  USB Bosto tablet support
 *
 *  Original Copyright (c) 2010 Xing Wei <weixing@hanwang.com.cn>
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#include <linux/jiffies.h>
#include <linux/stat.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/fcntl.h>
#include <linux/unistd.h>

#define DRIVER_DESC     "USB Bosto(2nd Gen) tablet driver"
#define DRIVER_LICENSE  "GPL"

MODULE_AUTHOR("Xing Wei <weixing@hanwang.com.cn>");
MODULE_AUTHOR("Aidan Walton <aidan@wires3.net>");
MODULE_AUTHOR("Leslie Viljoen <leslieviljoen@gmail.com>");

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE(DRIVER_LICENSE);

#define USB_VENDOR_ID_HANWANG		0x0b57
#define USB_PRODUCT_BOSTO14WA		0x9018

#define BOSTO_TABLET_INT_CLASS	0x0003
#define BOSTO_TABLET_INT_SUB_CLASS	0x0001
#define BOSTO_TABLET_INT_PROTOCOL	0x0002

#define PKGLEN_MAX	10

/*  Delay between TOOL_IN event and first reported pressure > 0. (mS) Used to supress settle time for pen ABS positions. */
#define PEN_WRITE_DELAY		230

/* device IDs */
#define STYLUS_DEVICE_ID	0x02
#define TOUCH_DEVICE_ID		0x03
#define CURSOR_DEVICE_ID	0x06
#define ERASER_DEVICE_ID	0x0A
#define PAD_DEVICE_ID		0x0F

/* match vendor and interface info */

#define BOSTO_TABLET_DEVICE(vend, prod, cl, sc, pr) \
    .match_flags = USB_DEVICE_ID_MATCH_INT_INFO \
            | USB_DEVICE_ID_MATCH_DEVICE, \
        .idVendor = (vend), \
        .idProduct = (prod), \
        .bInterfaceClass = (cl), \
        .bInterfaceSubClass = (sc), \
        .bInterfaceProtocol = (pr)

enum bosto_tablet_type {
	BOSTO_14WA
};

struct bosto {
	unsigned char *data;
	dma_addr_t data_dma;
	struct input_dev *dev;
	struct usb_device *usbdev;
	struct urb *irq;
	const struct bosto_features *features;
	unsigned int current_tool;
	unsigned int current_id;
	char name[64];
	char phys[32];
};

struct bosto_features {
	unsigned short pid;
	char *name;
	enum bosto_tablet_type type;
	int pkg_len;
	int max_x;
	int max_y;
	int max_tilt_x;
	int max_tilt_y;
	int max_pressure;
};

static const struct bosto_features features_array[] = {
	{ USB_PRODUCT_BOSTO14WA, "Bosto Kingtee 14WA", BOSTO_14WA,
		PKGLEN_MAX, 0x27de, 0x1cfe, 0x3f, 0x7f, 2048 },
};

static const int hw_eventtypes[] = {
	EV_KEY, EV_ABS, EV_MSC,
};

static const int hw_absevents[] = {
	ABS_X, ABS_Y, ABS_PRESSURE, ABS_MISC
};

static const int hw_btnevents[] = {
/*
	BTN_STYLUS 	seems to be the same as a center mouse button above the roll wheel.
 	BTN_STYLUS2	right mouse button (Gedit)
 	BTN_DIGI		seems to do nothing in relation to the stylus tool 
	BTN_TOUCH	left mouse click, or pen contact with the tablet surface. Remains asserted whenever the pen has contact..
*/
		
	BTN_DIGI, BTN_TOUCH, BTN_STYLUS, BTN_STYLUS2, BTN_TOOL_PEN, BTN_TOOL_BRUSH, 
	BTN_TOOL_RUBBER, BTN_TOOL_PENCIL, BTN_TOOL_AIRBRUSH, BTN_TOOL_FINGER, BTN_TOOL_MOUSE
};

static const int hw_mscevents[] = {
	MSC_SERIAL,
};

static void bosto_tool_out(struct input_dev *input_dev, unsigned int *id, unsigned int *tool) {
	*id = 0;
	input_report_key(input_dev, *tool, 0);
}

static void bosto_tool_in(struct input_dev *input_dev, unsigned int *id, unsigned int *tool, unsigned long *stamp, u8 pen_end){
	*stamp = jiffies + PEN_WRITE_DELAY * HZ / 1000; // Time stamp the 'TOOL IN' event and add delay.

	switch (pen_end & 0xf0) {
	/* Stylus Tip in prox */
	case 0x20:
		*id = STYLUS_DEVICE_ID;
		*tool = BTN_TOOL_PEN;
		input_report_key(input_dev, BTN_TOOL_PEN, 1);
		//dev_dbg(&dev->dev, "TOOL IN:Exit ID:Tool %x:%x\n", bosto->current_id, bosto->current_tool );
		break;

	/* Stylus Eraser in prox */
	case 0xa0: 
		*id = ERASER_DEVICE_ID;
		*tool = BTN_TOOL_RUBBER;
		input_report_key(input_dev, BTN_TOOL_RUBBER, 1);
		//dev_dbg(&dev->dev, "TOOL IN ERASER:Exit ID:Tool %x:%x\n", bosto->current_id, bosto->current_tool);
		break;

	default:
		*id = 0;
		//dev_dbg(&dev->dev, "Unknown tablet tool %02x ", data[0]);
		break;
	}
}

static void bosto_pen_float(struct input_dev *input_dev, unsigned int *tool, u16 *p, u16 *x, u16 *y, u8 *data) {
	*x = (data[1] << 8) | data[2];		// Set x ABS
	*y = (data[3] << 8) | data[4];		// Set y ABS
	*p = 0;

	input_report_key(input_dev, *tool, 1);
	input_report_key(input_dev, BTN_TOUCH, 0);

	switch (data[0]) {
	case 0xa0 ... 0xa1:
		input_report_key(input_dev, BTN_STYLUS2, 0);
		break;
	case 0xa2 ... 0xa3:
		input_report_key(input_dev, BTN_STYLUS2, 1);
		break;
	}
}

static void bosto_pen_contact(struct input_dev *input_dev, unsigned int *tool, u16 *p, u16 *x, u16 *y, unsigned long stamp, u8 *data) {
	/* All a little strange; these 4 bytes are always seen whenever the pen is in contact with the tablet. 
	 * 'e0 + e1', without the stylus button pressed and 'e2 + e3' with the stylus button pressed. Either of the buttons.
	 * In either case the byte value jitters between a pair of either of the two states dependent on the button press. */

	input_report_key(input_dev, *tool, 1);
	input_report_key(input_dev, BTN_TOUCH, 1);
	
	*x = (data[1] << 8) | data[2];		// Set x ABS
	*y = (data[3] << 8) | data[4];		// Set y ABS
	
	/* Set 2048 Level pressure sensitivity. 						
	 * NOTE: 	The pen button magnifies the pressure sensitivity. Bring the pen in with the button pressed,
	Ignore the right click response and keep the button held down. Enjoy the pressure magnification. */
	if (jiffies > stamp ) {
		*p = (data[5] << 3) | ((data[6] & 0xc0) >> 5);
	}
	else {
		*p = 0;
	}
	
	switch (data[0]) {
	case 0xe0 ... 0xe1:
		input_report_key(input_dev, BTN_STYLUS2, 0);	// no stylus btn
		break;
	case 0xe2 ... 0xe3:
		input_report_key(input_dev, BTN_STYLUS2, 1);	// stylus btn
		break;
	}
}

static void bosto_parse_packet(struct bosto *bosto)
{
	unsigned char *data = bosto->data;
	struct input_dev *input_dev = bosto->dev;
	struct usb_device *dev = bosto->usbdev;
	u16 x = 0;
	u16 y = 0;
	u16 p = 0;
	static unsigned long stamp;

	dev_dbg(&dev->dev, "Bosto packet: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
			data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8], data[9]);

	switch (data[0]) {
		
	/* pen event */
	case 0x02:
		//printk (KERN_DEBUG "Pen Event Packet\n" );		
		switch (data[1]) {
		case 0x80:
			bosto_tool_out(input_dev, &bosto->current_id, &bosto->current_tool);						
			dev_dbg(&dev->dev, "TOOL OUT. PEN ID:Tool %x:%x\n", bosto->current_id, bosto->current_tool);
			break;
			
		case 0xc2:	
			bosto_tool_in(input_dev, &bosto->current_id, &bosto->current_tool, &stamp, data[3]);
			dev_dbg(&dev->dev, "TOOL IN: ID:Tool %x:%x\n", bosto->current_id, bosto->current_tool);
			break;
			
		/* Pen trackable but not in contact with screen */
		case 0xa0 ... 0xa3:
			bosto_pen_float(input_dev, &bosto->current_tool, &p, &x, &y, &data[1]);				
			dev_dbg(&dev->dev, "PEN FLOAT: ID:Tool %x:%x\n", bosto->current_id, bosto->current_tool);
			break;

		/* Pen contact */
		case 0xe0 ... 0xe3:		
			bosto_pen_contact(input_dev, &bosto->current_tool, &p, &x, &y, stamp, &data[1]);
			dev_dbg(&dev->dev, "PEN TOUCH: ID:Tool %x:%x\n", bosto->current_id, bosto->current_tool);
			break;
		}
		break;
		
	case 0x0c:
                dev_dbg(&dev->dev,"Tablet Event. Packet data[0]: %02x\n", data[0] );
		input_report_abs(input_dev, ABS_MISC, bosto->current_id);
		input_event(input_dev, EV_MSC, MSC_SERIAL, bosto->features->pid);
		input_sync(input_dev);
		
	default:
		dev_dbg(&dev->dev, "Error packet. Packet data[0]:  %02x ", data[0]);
		break;
	}
        
	input_report_abs(input_dev, ABS_X, le16_to_cpup((__le16 *)&x));
	input_report_abs(input_dev, ABS_Y, le16_to_cpup((__le16 *)&y));
	input_report_abs(input_dev, ABS_PRESSURE, p ); //le16_to_cpup((__le16 *)&p));
	input_report_abs(input_dev, ABS_MISC, bosto->current_id);
	input_event(input_dev, EV_MSC, MSC_SERIAL, bosto->features->pid);		
		
	input_sync(input_dev);
}

static void bosto_irq(struct urb *urb)
{
	struct bosto *bosto = urb->context;
	struct usb_device *dev = bosto->usbdev;
	int retval;

	switch (urb->status) {
	case 0:
		/* success */;
		bosto_parse_packet(bosto);
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dev_err(&dev->dev, "%s - urb shutting down with status: %d",
			__func__, urb->status);
		return;
	default:
		dev_err(&dev->dev, "%s - nonzero urb status received: %d",
			__func__, urb->status);
		break;
	}

	retval = usb_submit_urb(urb, GFP_ATOMIC);
	if (retval)
		dev_err(&dev->dev, "%s - usb_submit_urb failed with result %d",
			__func__, retval);
}

static int bosto_open(struct input_dev *dev)
{
	struct bosto *bosto = input_get_drvdata(dev);

	bosto->irq->dev = bosto->usbdev;
	if (usb_submit_urb(bosto->irq, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void bosto_close(struct input_dev *dev)
{
	struct bosto *bosto = input_get_drvdata(dev);

	usb_kill_urb(bosto->irq);
}

static bool get_features(struct usb_device *dev, struct bosto *bosto)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(features_array); i++) {
		if (le16_to_cpu(dev->descriptor.idProduct) == features_array[i].pid) {
			bosto->features = &features_array[i];
			return true;
		}
	}

	return false;
}


static int bosto_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct usb_endpoint_descriptor *endpoint;
	struct bosto *bosto;
	struct input_dev *input_dev;
	int error;
	int i;

	printk (KERN_INFO "Bosto_Probe checking Tablet.\n");
	bosto = kzalloc(sizeof(struct bosto), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!bosto || !input_dev) {
		error = -ENOMEM;
		goto fail1;
	}

	if (!get_features(dev, bosto)) {
		error = -ENXIO;
		goto fail1;
	}

	bosto->data = usb_alloc_coherent(dev, bosto->features->pkg_len,
					GFP_KERNEL, &bosto->data_dma);
	if (!bosto->data) {
		error = -ENOMEM;
		goto fail1;
	}

	bosto->irq = usb_alloc_urb(0, GFP_KERNEL);
	if (!bosto->irq) {
		error = -ENOMEM;
		goto fail2;
	}

	bosto->usbdev = dev;
	bosto->dev = input_dev;

	usb_make_path(dev, bosto->phys, sizeof(bosto->phys));
	strlcat(bosto->phys, "/input0", sizeof(bosto->phys));

	strlcpy(bosto->name, bosto->features->name, sizeof(bosto->name));
	input_dev->name = bosto->name;
	input_dev->phys = bosto->phys;
	usb_to_input_id(dev, &input_dev->id);
	input_dev->dev.parent = &intf->dev;

	input_set_drvdata(input_dev, bosto);

	input_dev->open = bosto_open;
	input_dev->close = bosto_close;

	for (i = 0; i < ARRAY_SIZE(hw_eventtypes); ++i)
		__set_bit(hw_eventtypes[i], input_dev->evbit);

	for (i = 0; i < ARRAY_SIZE(hw_absevents); ++i)
		__set_bit(hw_absevents[i], input_dev->absbit);

	for (i = 0; i < ARRAY_SIZE(hw_btnevents); ++i)
		__set_bit(hw_btnevents[i], input_dev->keybit);

	for (i = 0; i < ARRAY_SIZE(hw_mscevents); ++i)
		__set_bit(hw_mscevents[i], input_dev->mscbit);

	input_set_abs_params(input_dev, ABS_X,
			     0, bosto->features->max_x, 0, 0);
	input_set_abs_params(input_dev, ABS_Y,
			     0, bosto->features->max_y, 0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE,
			     0, bosto->features->max_pressure, 0, 0);

	endpoint = &intf->cur_altsetting->endpoint[0].desc;
	usb_fill_int_urb(bosto->irq, dev,
			usb_rcvintpipe(dev, endpoint->bEndpointAddress),
			bosto->data, bosto->features->pkg_len,
			bosto_irq, bosto, endpoint->bInterval);
	bosto->irq->transfer_dma = bosto->data_dma;
	bosto->irq->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = input_register_device(bosto->dev);
	if (error)
		goto fail3;

	usb_set_intfdata(intf, bosto);

	return 0;

 fail3:	usb_free_urb(bosto->irq);
 fail2:	usb_free_coherent(dev, bosto->features->pkg_len,
			bosto->data, bosto->data_dma);
 fail1:	input_free_device(input_dev);
	kfree(bosto);
	return error;

}

static void bosto_disconnect(struct usb_interface *intf)
{
	struct bosto *bosto = usb_get_intfdata(intf);

	input_unregister_device(bosto->dev);
	usb_free_urb(bosto->irq);
	usb_free_coherent(interface_to_usbdev(intf),
			bosto->features->pkg_len, bosto->data,
			bosto->data_dma);
	kfree(bosto);
	usb_set_intfdata(intf, NULL);
}

static const struct usb_device_id bosto_ids[] = {
	{ BOSTO_TABLET_DEVICE(
		USB_VENDOR_ID_HANWANG, 
		USB_PRODUCT_BOSTO14WA, 
		BOSTO_TABLET_INT_CLASS, 
		BOSTO_TABLET_INT_SUB_CLASS, 
		BOSTO_TABLET_INT_PROTOCOL) },
	{}
};



MODULE_DEVICE_TABLE(usb, bosto_ids);

static struct usb_driver bosto_driver = {
	.name		= "bosto_14wa",
	.probe		= bosto_probe,
	.disconnect	= bosto_disconnect,
	.id_table	= bosto_ids,
};

static int __init bosto_init(void)
{
	printk(KERN_INFO "Bosto 2nd Generation USB Driver module being initialised.\n" );
	return usb_register(&bosto_driver);
}

static void __exit bosto_exit(void)
{
	usb_deregister(&bosto_driver);
}

module_init(bosto_init);
module_exit(bosto_exit);
