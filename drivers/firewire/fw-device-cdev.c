/*						-*- c-basic-offset: 8 -*-
 *
 * fw-device-cdev.c - Char device for device raw access
 *
 * Copyright (C) 2005-2006  Kristian Hoegsberg <krh@bitplanet.net>
 *
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/vmalloc.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/idr.h>
#include <linux/compat.h>
#include <asm/uaccess.h>
#include "fw-transaction.h"
#include "fw-topology.h"
#include "fw-device.h"
#include "fw-device-cdev.h"

/*
 * todo
 *
 * - bus resets sends a new packet with new generation and node id
 *
 */

/* dequeue_event() just kfree()'s the event, so the event has to be
 * the first field in the struct. */

struct event {
	struct { void *data; size_t size; } v[2];
	struct list_head link;
};

struct bus_reset {
	struct event event;
	struct fw_cdev_event_bus_reset reset;
};

struct response {
	struct event event;
	struct fw_transaction transaction;
	struct client *client;
	struct list_head link;
	struct fw_cdev_event_response response;
};

struct iso_interrupt {
	struct event event;
	struct fw_cdev_event_iso_interrupt interrupt;
};

struct client {
	u32 version;
	struct fw_device *device;
	spinlock_t lock;
	struct list_head handler_list;
	struct list_head request_list;
	struct list_head transaction_list;
	u32 request_serial;
	struct list_head event_list;
	wait_queue_head_t wait;

	struct fw_iso_context *iso_context;
	struct fw_iso_buffer buffer;
	unsigned long vm_start;

	struct list_head link;
};

static inline void __user *
u64_to_uptr(__u64 value)
{
	return (void __user *)(unsigned long)value;
}

static inline __u64
uptr_to_u64(void __user *ptr)
{
	return (__u64)(unsigned long)ptr;
}

static int fw_device_op_open(struct inode *inode, struct file *file)
{
	struct fw_device *device;
	struct client *client;
	unsigned long flags;

	device = fw_device_from_devt(inode->i_rdev);
	if (device == NULL)
		return -ENODEV;

	client = kzalloc(sizeof *client, GFP_KERNEL);
	if (client == NULL)
		return -ENOMEM;

	client->device = fw_device_get(device);
	INIT_LIST_HEAD(&client->event_list);
	INIT_LIST_HEAD(&client->handler_list);
	INIT_LIST_HEAD(&client->request_list);
	INIT_LIST_HEAD(&client->transaction_list);
	spin_lock_init(&client->lock);
	init_waitqueue_head(&client->wait);

	file->private_data = client;

	spin_lock_irqsave(&device->card->lock, flags);
	list_add_tail(&client->link, &device->client_list);
	spin_unlock_irqrestore(&device->card->lock, flags);

	return 0;
}

static void queue_event(struct client *client, struct event *event,
			void *data0, size_t size0, void *data1, size_t size1)
{
	unsigned long flags;

	event->v[0].data = data0;
	event->v[0].size = size0;
	event->v[1].data = data1;
	event->v[1].size = size1;

	spin_lock_irqsave(&client->lock, flags);

	list_add_tail(&event->link, &client->event_list);
	wake_up_interruptible(&client->wait);

	spin_unlock_irqrestore(&client->lock, flags);
}

static int
dequeue_event(struct client *client, char __user *buffer, size_t count)
{
	unsigned long flags;
	struct event *event;
	size_t size, total;
	int i, retval;

	retval = wait_event_interruptible(client->wait,
					  !list_empty(&client->event_list) ||
					  fw_device_is_shutdown(client->device));
	if (retval < 0)
		return retval;

	if (list_empty(&client->event_list) &&
		       fw_device_is_shutdown(client->device))
		return -ENODEV;

	spin_lock_irqsave(&client->lock, flags);
	event = container_of(client->event_list.next, struct event, link);
	list_del(&event->link);
	spin_unlock_irqrestore(&client->lock, flags);

	total = 0;
	for (i = 0; i < ARRAY_SIZE(event->v) && total < count; i++) {
		size = min(event->v[i].size, count - total);
		if (copy_to_user(buffer + total, event->v[i].data, size)) {
			retval = -EFAULT;
			goto out;
		}
		total += size;
	}
	retval = total;

 out:
	kfree(event);

	return retval;
}

static ssize_t
fw_device_op_read(struct file *file,
		  char __user *buffer, size_t count, loff_t *offset)
{
	struct client *client = file->private_data;

	return dequeue_event(client, buffer, count);
}

static void
fill_bus_reset_event(struct fw_cdev_event_bus_reset *event,
		     struct fw_device *device)
{
	struct fw_card *card = device->card;

	event->type          = FW_CDEV_EVENT_BUS_RESET;
	event->node_id       = device->node_id;
	event->local_node_id = card->local_node->node_id;
	event->bm_node_id    = 0; /* FIXME: We don't track the BM. */
	event->irm_node_id   = card->irm_node->node_id;
	event->root_node_id  = card->root_node->node_id;
	event->generation    = card->generation;
}

static void
for_each_client(struct fw_device *device,
		void (*callback)(struct client *client))
{
	struct fw_card *card = device->card;
	struct client *c;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);

	list_for_each_entry(c, &device->client_list, link)
		callback(c);

	spin_unlock_irqrestore(&card->lock, flags);
}

static void
queue_bus_reset_event(struct client *client)
{
	struct bus_reset *bus_reset;
	struct fw_device *device = client->device;

	bus_reset = kzalloc(sizeof *bus_reset, GFP_ATOMIC);
	if (bus_reset == NULL) {
		fw_notify("Out of memory when allocating bus reset event\n");
		return;
	}

	fill_bus_reset_event(&bus_reset->reset, device);

	queue_event(client, &bus_reset->event,
		    &bus_reset->reset, sizeof bus_reset->reset, NULL, 0);
}

void fw_device_cdev_update(struct fw_device *device)
{
	for_each_client(device, queue_bus_reset_event);
}

static void wake_up_client(struct client *client)
{
	wake_up_interruptible(&client->wait);
}

void fw_device_cdev_remove(struct fw_device *device)
{
	for_each_client(device, wake_up_client);
}

static int ioctl_get_info(struct client *client, void __user *arg)
{
	struct fw_cdev_get_info get_info;
	struct fw_cdev_event_bus_reset bus_reset;

	if (copy_from_user(&get_info, arg, sizeof get_info))
		return -EFAULT;

	client->version = get_info.version;
	get_info.version = FW_CDEV_VERSION;

	if (get_info.rom != 0) {
		void __user *uptr = u64_to_uptr(get_info.rom);
		size_t length = min(get_info.rom_length,
				    client->device->config_rom_length * 4);

		if (copy_to_user(uptr, client->device->config_rom, length))
			return -EFAULT;
	}
	get_info.rom_length = client->device->config_rom_length * 4;

	if (get_info.bus_reset != 0) {
		void __user *uptr = u64_to_uptr(get_info.bus_reset);

		fill_bus_reset_event(&bus_reset, client->device);
		if (copy_to_user(uptr, &bus_reset, sizeof bus_reset))
			return -EFAULT;
	}

	get_info.card = client->device->card->index;

	if (copy_to_user(arg, &get_info, sizeof get_info))
		return -EFAULT;

	return 0;
}

static void
complete_transaction(struct fw_card *card, int rcode,
		     void *payload, size_t length, void *data)
{
	struct response *response = data;
	struct client *client = response->client;
	unsigned long flags;

	if (length < response->response.length)
		response->response.length = length;
	if (rcode == RCODE_COMPLETE)
		memcpy(response->response.data, payload,
		       response->response.length);

	spin_lock_irqsave(&client->lock, flags);
	list_del(&response->link);
	spin_unlock_irqrestore(&client->lock, flags);

	response->response.type   = FW_CDEV_EVENT_RESPONSE;
	response->response.rcode  = rcode;
	queue_event(client, &response->event,
		    &response->response, sizeof response->response,
		    response->response.data, response->response.length);
}

static ssize_t ioctl_send_request(struct client *client, void __user *arg)
{
	struct fw_device *device = client->device;
	struct fw_cdev_send_request request;
	struct response *response;
	unsigned long flags;

	if (copy_from_user(&request, arg, sizeof request))
		return -EFAULT;

	/* What is the biggest size we'll accept, really? */
	if (request.length > 4096)
		return -EINVAL;

	response = kmalloc(sizeof *response + request.length, GFP_KERNEL);
	if (response == NULL)
		return -ENOMEM;

	response->client = client;
	response->response.length = request.length;
	response->response.closure = request.closure;

	if (request.data &&
	    copy_from_user(response->response.data,
			   u64_to_uptr(request.data), request.length)) {
		kfree(response);
		return -EFAULT;
	}

	spin_lock_irqsave(&client->lock, flags);
	list_add_tail(&response->link, &client->transaction_list);
	spin_unlock_irqrestore(&client->lock, flags);

	fw_send_request(device->card, &response->transaction,
			request.tcode & 0x1f,
			device->node->node_id,
			request.generation,
			device->node->max_speed,
			request.offset,
			response->response.data, request.length,
			complete_transaction, response);

	if (request.data)
		return sizeof request + request.length;
	else
		return sizeof request;
}

struct address_handler {
	struct fw_address_handler handler;
	__u64 closure;
	struct client *client;
	struct list_head link;
};

struct request {
	struct fw_request *request;
	void *data;
	size_t length;
	u32 serial;
	struct list_head link;
};

struct request_event {
	struct event event;
	struct fw_cdev_event_request request;
};

static void
handle_request(struct fw_card *card, struct fw_request *r,
	       int tcode, int destination, int source,
	       int generation, int speed,
	       unsigned long long offset,
	       void *payload, size_t length, void *callback_data)
{
	struct address_handler *handler = callback_data;
	struct request *request;
	struct request_event *e;
	unsigned long flags;
	struct client *client = handler->client;

	request = kmalloc(sizeof *request, GFP_ATOMIC);
	e = kmalloc(sizeof *e, GFP_ATOMIC);
	if (request == NULL || e == NULL) {
		kfree(request);
		kfree(e);
		fw_send_response(card, r, RCODE_CONFLICT_ERROR);
		return;
	}

	request->request = r;
	request->data    = payload;
	request->length  = length;

	spin_lock_irqsave(&client->lock, flags);
	request->serial = client->request_serial++;
	list_add_tail(&request->link, &client->request_list);
	spin_unlock_irqrestore(&client->lock, flags);

	e->request.type    = FW_CDEV_EVENT_REQUEST;
	e->request.tcode   = tcode;
	e->request.offset  = offset;
	e->request.length  = length;
	e->request.serial  = request->serial;
	e->request.closure = handler->closure;

	queue_event(client, &e->event,
		    &e->request, sizeof e->request, payload, length);
}

static int ioctl_allocate(struct client *client, void __user *arg)
{
	struct fw_cdev_allocate request;
	struct address_handler *handler;
	unsigned long flags;
	struct fw_address_region region;

	if (copy_from_user(&request, arg, sizeof request))
		return -EFAULT;

	handler = kmalloc(sizeof *handler, GFP_KERNEL);
	if (handler == NULL)
		return -ENOMEM;

	region.start = request.offset;
	region.end = request.offset + request.length;
	handler->handler.length = request.length;
	handler->handler.address_callback = handle_request;
	handler->handler.callback_data = handler;
	handler->closure = request.closure;
	handler->client = client;

	if (fw_core_add_address_handler(&handler->handler, &region) < 0) {
		kfree(handler);
		return -EBUSY;
	}

	spin_lock_irqsave(&client->lock, flags);
	list_add_tail(&handler->link, &client->handler_list);
	spin_unlock_irqrestore(&client->lock, flags);

	return 0;
}

static int ioctl_send_response(struct client *client, void __user *arg)
{
	struct fw_cdev_send_response request;
	struct request *r;
	unsigned long flags;

	if (copy_from_user(&request, arg, sizeof request))
		return -EFAULT;

	spin_lock_irqsave(&client->lock, flags);
	list_for_each_entry(r, &client->request_list, link) {
		if (r->serial == request.serial) {
			list_del(&r->link);
			break;
		}
	}
	spin_unlock_irqrestore(&client->lock, flags);

	if (&r->link == &client->request_list)
		return -EINVAL;

	if (request.length < r->length)
		r->length = request.length;
	if (copy_from_user(r->data, u64_to_uptr(request.data), r->length))
		return -EFAULT;

	fw_send_response(client->device->card, r->request, request.rcode);

	kfree(r);

	return 0;
}

static int ioctl_initiate_bus_reset(struct client *client, void __user *arg)
{
	struct fw_cdev_initiate_bus_reset request;
	int short_reset;

	if (copy_from_user(&request, arg, sizeof request))
		return -EFAULT;

	short_reset = (request.type == FW_CDEV_SHORT_RESET);

	return fw_core_initiate_bus_reset(client->device->card, short_reset);
}

static void
iso_callback(struct fw_iso_context *context, u32 cycle,
	     size_t header_length, void *header, void *data)
{
	struct client *client = data;
	struct iso_interrupt *interrupt;

	interrupt = kzalloc(sizeof *interrupt + header_length, GFP_ATOMIC);
	if (interrupt == NULL)
		return;

	interrupt->interrupt.type      = FW_CDEV_EVENT_ISO_INTERRUPT;
	interrupt->interrupt.closure   = 0;
	interrupt->interrupt.cycle     = cycle;
	interrupt->interrupt.header_length = header_length;
	memcpy(interrupt->interrupt.header, header, header_length);
	queue_event(client, &interrupt->event,
		    &interrupt->interrupt,
		    sizeof interrupt->interrupt + header_length, NULL, 0);
}

static int ioctl_create_iso_context(struct client *client, void __user *arg)
{
	struct fw_cdev_create_iso_context request;

	if (copy_from_user(&request, arg, sizeof request))
		return -EFAULT;

	if (request.channel > 63)
		return -EINVAL;

	switch (request.type) {
	case FW_ISO_CONTEXT_RECEIVE:
		if (request.header_size < 4 || (request.header_size & 3))
			return -EINVAL;

		break;

	case FW_ISO_CONTEXT_TRANSMIT:
		if (request.speed > SCODE_3200)
			return -EINVAL;

		break;

	default:
		return -EINVAL;
	}

	client->iso_context = fw_iso_context_create(client->device->card,
						    request.type,
						    request.channel,
						    request.speed,
						    request.header_size,
						    iso_callback, client);
	if (IS_ERR(client->iso_context))
		return PTR_ERR(client->iso_context);

	return 0;
}

static int ioctl_queue_iso(struct client *client, void __user *arg)
{
	struct fw_cdev_queue_iso request;
	struct fw_cdev_iso_packet __user *p, *end, *next;
	struct fw_iso_context *ctx = client->iso_context;
	unsigned long payload, payload_end, header_length;
	int count;
	struct {
		struct fw_iso_packet packet;
		u8 header[256];
	} u;

	if (ctx == NULL)
		return -EINVAL;
	if (copy_from_user(&request, arg, sizeof request))
		return -EFAULT;

	/* If the user passes a non-NULL data pointer, has mmap()'ed
	 * the iso buffer, and the pointer points inside the buffer,
	 * we setup the payload pointers accordingly.  Otherwise we
	 * set them both to 0, which will still let packets with
	 * payload_length == 0 through.  In other words, if no packets
	 * use the indirect payload, the iso buffer need not be mapped
	 * and the request.data pointer is ignored.*/

	payload = (unsigned long)request.data - client->vm_start;
	payload_end = payload + (client->buffer.page_count << PAGE_SHIFT);
	if (request.data == 0 || client->buffer.pages == NULL ||
	    payload >= payload_end) {
		payload = 0;
		payload_end = 0;
	}

	if (!access_ok(VERIFY_READ, request.packets, request.size))
		return -EFAULT;

	p = (struct fw_cdev_iso_packet __user *)u64_to_uptr(request.packets);
	end = (void __user *)p + request.size;
	count = 0;
	while (p < end) {
		if (__copy_from_user(&u.packet, p, sizeof *p))
			return -EFAULT;

		if (ctx->type == FW_ISO_CONTEXT_TRANSMIT) {
			header_length = u.packet.header_length;
		} else {
			/* We require that header_length is a multiple of
			 * the fixed header size, ctx->header_size */
			if (ctx->header_size == 0) {
				if (u.packet.header_length > 0)
					return -EINVAL;
			} else if (u.packet.header_length % ctx->header_size != 0) {
				return -EINVAL;
			}
			header_length = 0;
		}

		next = (struct fw_cdev_iso_packet __user *)
			&p->header[header_length / 4];
		if (next > end)
			return -EINVAL;
		if (__copy_from_user
		    (u.packet.header, p->header, header_length))
			return -EFAULT;
		if (u.packet.skip && ctx->type == FW_ISO_CONTEXT_TRANSMIT &&
		    u.packet.header_length + u.packet.payload_length > 0)
			return -EINVAL;
		if (payload + u.packet.payload_length > payload_end)
			return -EINVAL;

		if (fw_iso_context_queue(ctx, &u.packet,
					 &client->buffer, payload))
			break;

		p = next;
		payload += u.packet.payload_length;
		count++;
	}

	request.size    -= uptr_to_u64(p) - request.packets;
	request.packets  = uptr_to_u64(p);
	request.data     = client->vm_start + payload;

	if (copy_to_user(arg, &request, sizeof request))
		return -EFAULT;

	return count;
}

static int ioctl_start_iso(struct client *client, void __user *arg)
{
	struct fw_cdev_start_iso request;

	if (copy_from_user(&request, arg, sizeof request))
		return -EFAULT;

	if (client->iso_context->type == FW_ISO_CONTEXT_RECEIVE) {
		if (request.tags == 0 || request.tags > 15)
			return -EINVAL;

		if (request.sync > 15)
			return -EINVAL;
	}

	return fw_iso_context_start(client->iso_context,
				    request.cycle, request.sync, request.tags);
}

static int ioctl_stop_iso(struct client *client, void __user *arg)
{
	return fw_iso_context_stop(client->iso_context);
}

static int
dispatch_ioctl(struct client *client, unsigned int cmd, void __user *arg)
{
	switch (cmd) {
	case FW_CDEV_IOC_GET_INFO:
		return ioctl_get_info(client, arg);
	case FW_CDEV_IOC_SEND_REQUEST:
		return ioctl_send_request(client, arg);
	case FW_CDEV_IOC_ALLOCATE:
		return ioctl_allocate(client, arg);
	case FW_CDEV_IOC_SEND_RESPONSE:
		return ioctl_send_response(client, arg);
	case FW_CDEV_IOC_INITIATE_BUS_RESET:
		return ioctl_initiate_bus_reset(client, arg);
	case FW_CDEV_IOC_CREATE_ISO_CONTEXT:
		return ioctl_create_iso_context(client, arg);
	case FW_CDEV_IOC_QUEUE_ISO:
		return ioctl_queue_iso(client, arg);
	case FW_CDEV_IOC_START_ISO:
		return ioctl_start_iso(client, arg);
	case FW_CDEV_IOC_STOP_ISO:
		return ioctl_stop_iso(client, arg);
	default:
		return -EINVAL;
	}
}

static long
fw_device_op_ioctl(struct file *file,
		   unsigned int cmd, unsigned long arg)
{
	struct client *client = file->private_data;

	return dispatch_ioctl(client, cmd, (void __user *) arg);
}

#ifdef CONFIG_COMPAT
static long
fw_device_op_compat_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct client *client = file->private_data;

	return dispatch_ioctl(client, cmd, compat_ptr(arg));
}
#endif

static int fw_device_op_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct client *client = file->private_data;
	enum dma_data_direction direction;
	unsigned long size;
	int page_count, retval;

	/* FIXME: We could support multiple buffers, but we don't. */
	if (client->buffer.pages != NULL)
		return -EBUSY;

	if (!(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	if (vma->vm_start & ~PAGE_MASK)
		return -EINVAL;

	client->vm_start = vma->vm_start;
	size = vma->vm_end - vma->vm_start;
	page_count = size >> PAGE_SHIFT;
	if (size & ~PAGE_MASK)
		return -EINVAL;

	if (vma->vm_flags & VM_WRITE)
		direction = DMA_TO_DEVICE;
	else
		direction = DMA_FROM_DEVICE;

	retval = fw_iso_buffer_init(&client->buffer, client->device->card,
				    page_count, direction);
	if (retval < 0)
		return retval;

	retval = fw_iso_buffer_map(&client->buffer, vma);
	if (retval < 0)
		fw_iso_buffer_destroy(&client->buffer, client->device->card);

	return retval;
}

static int fw_device_op_release(struct inode *inode, struct file *file)
{
	struct client *client = file->private_data;
	struct address_handler *h, *next_h;
	struct request *r, *next_r;
	struct event *e, *next_e;
	struct response *t, *next_t;
	unsigned long flags;

	if (client->buffer.pages)
		fw_iso_buffer_destroy(&client->buffer, client->device->card);

	if (client->iso_context)
		fw_iso_context_destroy(client->iso_context);

	list_for_each_entry_safe(h, next_h, &client->handler_list, link) {
		fw_core_remove_address_handler(&h->handler);
		kfree(h);
	}

	list_for_each_entry_safe(r, next_r, &client->request_list, link) {
		fw_send_response(client->device->card, r->request,
				 RCODE_CONFLICT_ERROR);
		kfree(r);
	}

	list_for_each_entry_safe(t, next_t, &client->transaction_list, link)
		fw_cancel_transaction(client->device->card, &t->transaction);

	/* FIXME: We should wait for the async tasklets to stop
	 * running before freeing the memory. */

	list_for_each_entry_safe(e, next_e, &client->event_list, link)
		kfree(e);

	spin_lock_irqsave(&client->device->card->lock, flags);
	list_del(&client->link);
	spin_unlock_irqrestore(&client->device->card->lock, flags);

	fw_device_put(client->device);
	kfree(client);

	return 0;
}

static unsigned int fw_device_op_poll(struct file *file, poll_table * pt)
{
	struct client *client = file->private_data;
	unsigned int mask = 0;

	poll_wait(file, &client->wait, pt);

	if (fw_device_is_shutdown(client->device))
		mask |= POLLHUP | POLLERR;
	if (!list_empty(&client->event_list))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

const struct file_operations fw_device_ops = {
	.owner		= THIS_MODULE,
	.open		= fw_device_op_open,
	.read		= fw_device_op_read,
	.unlocked_ioctl	= fw_device_op_ioctl,
	.poll		= fw_device_op_poll,
	.release	= fw_device_op_release,
	.mmap		= fw_device_op_mmap,

#ifdef CONFIG_COMPAT
	.compat_ioctl	= fw_device_op_compat_ioctl,
#endif
};
