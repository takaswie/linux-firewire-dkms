/*
 * Char device for device raw access
 *
 * Copyright (C) 2005-2007  Kristian Hoegsberg <krh@bitplanet.net>
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
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/preempt.h>
#include <linux/time.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/idr.h>
#include <linux/compat.h>
#include <linux/firewire-cdev.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include "fw-transaction.h"
#include "fw-topology.h"
#include "fw-device.h"

struct client;
struct client_resource;
typedef void (*client_resource_release_fn_t)(struct client *,
					     struct client_resource *);
struct client_resource {
	client_resource_release_fn_t release;
	int handle;
};

/*
 * dequeue_event() just kfree()'s the event, so the event has to be
 * the first field in the struct.
 */

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
	struct client_resource resource;
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
	bool in_shutdown;
	struct idr resource_idr;
	struct list_head event_list;
	wait_queue_head_t wait;
	u64 bus_reset_closure;

	struct fw_iso_context *iso_context;
	u64 iso_closure;
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

	device = fw_device_get_by_devt(inode->i_rdev);
	if (device == NULL)
		return -ENODEV;

	if (fw_device_is_shutdown(device)) {
		fw_device_put(device);
		return -ENODEV;
	}

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (client == NULL) {
		fw_device_put(device);
		return -ENOMEM;
	}

	client->device = device;
	spin_lock_init(&client->lock);
	idr_init(&client->resource_idr);
	INIT_LIST_HEAD(&client->event_list);
	init_waitqueue_head(&client->wait);

	file->private_data = client;

	mutex_lock(&device->client_list_mutex);
	list_add_tail(&client->link, &device->client_list);
	mutex_unlock(&device->client_list_mutex);

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
	if (client->in_shutdown)
		kfree(event);
	else
		list_add_tail(&event->link, &client->event_list);
	spin_unlock_irqrestore(&client->lock, flags);

	wake_up_interruptible(&client->wait);
}

static int
dequeue_event(struct client *client, char __user *buffer, size_t count)
{
	unsigned long flags;
	struct event *event;
	size_t size, total;
	int i, ret;

	ret = wait_event_interruptible(client->wait,
			!list_empty(&client->event_list) ||
			fw_device_is_shutdown(client->device));
	if (ret < 0)
		return ret;

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
			ret = -EFAULT;
			goto out;
		}
		total += size;
	}
	ret = total;

 out:
	kfree(event);

	return ret;
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
		     struct client *client)
{
	struct fw_card *card = client->device->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);

	event->closure	     = client->bus_reset_closure;
	event->type          = FW_CDEV_EVENT_BUS_RESET;
	event->generation    = client->device->generation;
	event->node_id       = client->device->node_id;
	event->local_node_id = card->local_node->node_id;
	event->bm_node_id    = 0; /* FIXME: We don't track the BM. */
	event->irm_node_id   = card->irm_node->node_id;
	event->root_node_id  = card->root_node->node_id;

	spin_unlock_irqrestore(&card->lock, flags);
}

static void
for_each_client(struct fw_device *device,
		void (*callback)(struct client *client))
{
	struct client *c;

	mutex_lock(&device->client_list_mutex);
	list_for_each_entry(c, &device->client_list, link)
		callback(c);
	mutex_unlock(&device->client_list_mutex);
}

static void
queue_bus_reset_event(struct client *client)
{
	struct bus_reset *bus_reset;

	bus_reset = kzalloc(sizeof(*bus_reset), GFP_KERNEL);
	if (bus_reset == NULL) {
		fw_notify("Out of memory when allocating bus reset event\n");
		return;
	}

	fill_bus_reset_event(&bus_reset->reset, client);

	queue_event(client, &bus_reset->event,
		    &bus_reset->reset, sizeof(bus_reset->reset), NULL, 0);
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

static int ioctl_get_info(struct client *client, void *buffer)
{
	struct fw_cdev_get_info *get_info = buffer;
	struct fw_cdev_event_bus_reset bus_reset;
	unsigned long ret = 0;

	client->version = get_info->version;
	get_info->version = FW_CDEV_VERSION;
	get_info->card = client->device->card->index;

	down_read(&fw_device_rwsem);

	if (get_info->rom != 0) {
		void __user *uptr = u64_to_uptr(get_info->rom);
		size_t want = get_info->rom_length;
		size_t have = client->device->config_rom_length * 4;

		ret = copy_to_user(uptr, client->device->config_rom,
				   min(want, have));
	}
	get_info->rom_length = client->device->config_rom_length * 4;

	up_read(&fw_device_rwsem);

	if (ret != 0)
		return -EFAULT;

	client->bus_reset_closure = get_info->bus_reset_closure;
	if (get_info->bus_reset != 0) {
		void __user *uptr = u64_to_uptr(get_info->bus_reset);

		fill_bus_reset_event(&bus_reset, client);
		if (copy_to_user(uptr, &bus_reset, sizeof(bus_reset)))
			return -EFAULT;
	}

	return 0;
}

static int
add_client_resource(struct client *client, struct client_resource *resource,
		    gfp_t gfp_mask)
{
	unsigned long flags;
	int ret;

 retry:
	if (idr_pre_get(&client->resource_idr, gfp_mask) == 0)
		return -ENOMEM;

	spin_lock_irqsave(&client->lock, flags);
	if (client->in_shutdown)
		ret = -ECANCELED;
	else
		ret = idr_get_new(&client->resource_idr, resource,
				  &resource->handle);
	spin_unlock_irqrestore(&client->lock, flags);

	if (ret == -EAGAIN)
		goto retry;

	return ret < 0 ? ret : 0;
}

static int
release_client_resource(struct client *client, u32 handle,
			client_resource_release_fn_t release,
			struct client_resource **resource)
{
	struct client_resource *r;
	unsigned long flags;

	spin_lock_irqsave(&client->lock, flags);
	if (client->in_shutdown)
		r = NULL;
	else
		r = idr_find(&client->resource_idr, handle);
	if (r && r->release == release)
		idr_remove(&client->resource_idr, handle);
	spin_unlock_irqrestore(&client->lock, flags);

	if (!(r && r->release == release))
		return -EINVAL;

	if (resource)
		*resource = r;
	else
		r->release(client, r);

	return 0;
}

static void
release_transaction(struct client *client, struct client_resource *resource)
{
	struct response *response =
		container_of(resource, struct response, resource);

	fw_cancel_transaction(client->device->card, &response->transaction);
}

static void
complete_transaction(struct fw_card *card, int rcode,
		     void *payload, size_t length, void *data)
{
	struct response *response = data;
	struct client *client = response->client;
	unsigned long flags;
	struct fw_cdev_event_response *r = &response->response;

	if (length < r->length)
		r->length = length;
	if (rcode == RCODE_COMPLETE)
		memcpy(r->data, payload, r->length);

	spin_lock_irqsave(&client->lock, flags);
	/*
	 * If called while in shutdown, the idr tree must be left untouched.
	 * The idr handle will be removed later.
	 */
	if (!client->in_shutdown)
		idr_remove(&client->resource_idr, response->resource.handle);
	spin_unlock_irqrestore(&client->lock, flags);

	r->type   = FW_CDEV_EVENT_RESPONSE;
	r->rcode  = rcode;

	/*
	 * In the case that sizeof(*r) doesn't align with the position of the
	 * data, and the read is short, preserve an extra copy of the data
	 * to stay compatible with a pre-2.6.27 bug.  Since the bug is harmless
	 * for short reads and some apps depended on it, this is both safe
	 * and prudent for compatibility.
	 */
	if (r->length <= sizeof(*r) - offsetof(typeof(*r), data))
		queue_event(client, &response->event, r, sizeof(*r),
			    r->data, r->length);
	else
		queue_event(client, &response->event, r, sizeof(*r) + r->length,
			    NULL, 0);
}

static int ioctl_send_request(struct client *client, void *buffer)
{
	struct fw_device *device = client->device;
	struct fw_cdev_send_request *request = buffer;
	struct response *response;
	int ret;

	/* What is the biggest size we'll accept, really? */
	if (request->length > 4096)
		return -EINVAL;

	response = kmalloc(sizeof(*response) + request->length, GFP_KERNEL);
	if (response == NULL)
		return -ENOMEM;

	response->client = client;
	response->response.length = request->length;
	response->response.closure = request->closure;

	if (request->data &&
	    copy_from_user(response->response.data,
			   u64_to_uptr(request->data), request->length)) {
		ret = -EFAULT;
		goto failed;
	}

	switch (request->tcode) {
	case TCODE_WRITE_QUADLET_REQUEST:
	case TCODE_WRITE_BLOCK_REQUEST:
	case TCODE_READ_QUADLET_REQUEST:
	case TCODE_READ_BLOCK_REQUEST:
	case TCODE_LOCK_MASK_SWAP:
	case TCODE_LOCK_COMPARE_SWAP:
	case TCODE_LOCK_FETCH_ADD:
	case TCODE_LOCK_LITTLE_ADD:
	case TCODE_LOCK_BOUNDED_ADD:
	case TCODE_LOCK_WRAP_ADD:
	case TCODE_LOCK_VENDOR_DEPENDENT:
		break;
	default:
		ret = -EINVAL;
		goto failed;
	}

	response->resource.release = release_transaction;
	ret = add_client_resource(client, &response->resource, GFP_KERNEL);
	if (ret < 0)
		goto failed;

	fw_send_request(device->card, &response->transaction,
			request->tcode & 0x1f,
			device->node->node_id,
			request->generation,
			device->max_speed,
			request->offset,
			response->response.data, request->length,
			complete_transaction, response);

	if (request->data)
		return sizeof(request) + request->length;
	else
		return sizeof(request);
 failed:
	kfree(response);

	return ret;
}

struct address_handler {
	struct fw_address_handler handler;
	__u64 closure;
	struct client *client;
	struct client_resource resource;
};

struct request {
	struct fw_request *request;
	void *data;
	size_t length;
	struct client_resource resource;
};

struct request_event {
	struct event event;
	struct fw_cdev_event_request request;
};

static void
release_request(struct client *client, struct client_resource *resource)
{
	struct request *request =
		container_of(resource, struct request, resource);

	fw_send_response(client->device->card, request->request,
			 RCODE_CONFLICT_ERROR);
	kfree(request);
}

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
	struct client *client = handler->client;
	int ret;

	request = kmalloc(sizeof(*request), GFP_ATOMIC);
	e = kmalloc(sizeof(*e), GFP_ATOMIC);
	if (request == NULL || e == NULL)
		goto failed;

	request->request = r;
	request->data    = payload;
	request->length  = length;

	request->resource.release = release_request;
	ret = add_client_resource(client, &request->resource, GFP_ATOMIC);
	if (ret < 0)
		goto failed;

	e->request.type    = FW_CDEV_EVENT_REQUEST;
	e->request.tcode   = tcode;
	e->request.offset  = offset;
	e->request.length  = length;
	e->request.handle  = request->resource.handle;
	e->request.closure = handler->closure;

	queue_event(client, &e->event,
		    &e->request, sizeof(e->request), payload, length);
	return;

 failed:
	kfree(request);
	kfree(e);
	fw_send_response(card, r, RCODE_CONFLICT_ERROR);
}

static void
release_address_handler(struct client *client,
			struct client_resource *resource)
{
	struct address_handler *handler =
		container_of(resource, struct address_handler, resource);

	fw_core_remove_address_handler(&handler->handler);
	kfree(handler);
}

static int ioctl_allocate(struct client *client, void *buffer)
{
	struct fw_cdev_allocate *request = buffer;
	struct address_handler *handler;
	struct fw_address_region region;
	int ret;

	handler = kmalloc(sizeof(*handler), GFP_KERNEL);
	if (handler == NULL)
		return -ENOMEM;

	region.start = request->offset;
	region.end = request->offset + request->length;
	handler->handler.length = request->length;
	handler->handler.address_callback = handle_request;
	handler->handler.callback_data = handler;
	handler->closure = request->closure;
	handler->client = client;

	ret = fw_core_add_address_handler(&handler->handler, &region);
	if (ret < 0) {
		kfree(handler);
		return ret;
	}

	handler->resource.release = release_address_handler;
	ret = add_client_resource(client, &handler->resource, GFP_KERNEL);
	if (ret < 0) {
		release_address_handler(client, &handler->resource);
		return ret;
	}
	request->handle = handler->resource.handle;

	return 0;
}

static int ioctl_deallocate(struct client *client, void *buffer)
{
	struct fw_cdev_deallocate *request = buffer;

	return release_client_resource(client, request->handle,
				       release_address_handler, NULL);
}

static int ioctl_send_response(struct client *client, void *buffer)
{
	struct fw_cdev_send_response *request = buffer;
	struct client_resource *resource;
	struct request *r;

	if (release_client_resource(client, request->handle,
				    release_request, &resource) < 0)
		return -EINVAL;

	r = container_of(resource, struct request, resource);
	if (request->length < r->length)
		r->length = request->length;
	if (copy_from_user(r->data, u64_to_uptr(request->data), r->length))
		return -EFAULT;

	fw_send_response(client->device->card, r->request, request->rcode);
	kfree(r);

	return 0;
}

static int ioctl_initiate_bus_reset(struct client *client, void *buffer)
{
	struct fw_cdev_initiate_bus_reset *request = buffer;
	int short_reset;

	short_reset = (request->type == FW_CDEV_SHORT_RESET);

	return fw_core_initiate_bus_reset(client->device->card, short_reset);
}

struct descriptor {
	struct fw_descriptor d;
	struct client_resource resource;
	u32 data[0];
};

static void release_descriptor(struct client *client,
			       struct client_resource *resource)
{
	struct descriptor *descriptor =
		container_of(resource, struct descriptor, resource);

	fw_core_remove_descriptor(&descriptor->d);
	kfree(descriptor);
}

static int ioctl_add_descriptor(struct client *client, void *buffer)
{
	struct fw_cdev_add_descriptor *request = buffer;
	struct descriptor *descriptor;
	int ret;

	if (request->length > 256)
		return -EINVAL;

	descriptor =
		kmalloc(sizeof(*descriptor) + request->length * 4, GFP_KERNEL);
	if (descriptor == NULL)
		return -ENOMEM;

	if (copy_from_user(descriptor->data,
			   u64_to_uptr(request->data), request->length * 4)) {
		ret = -EFAULT;
		goto failed;
	}

	descriptor->d.length = request->length;
	descriptor->d.immediate = request->immediate;
	descriptor->d.key = request->key;
	descriptor->d.data = descriptor->data;

	ret = fw_core_add_descriptor(&descriptor->d);
	if (ret < 0)
		goto failed;

	descriptor->resource.release = release_descriptor;
	ret = add_client_resource(client, &descriptor->resource, GFP_KERNEL);
	if (ret < 0) {
		fw_core_remove_descriptor(&descriptor->d);
		goto failed;
	}
	request->handle = descriptor->resource.handle;

	return 0;
 failed:
	kfree(descriptor);

	return ret;
}

static int ioctl_remove_descriptor(struct client *client, void *buffer)
{
	struct fw_cdev_remove_descriptor *request = buffer;

	return release_client_resource(client, request->handle,
				       release_descriptor, NULL);
}

static void
iso_callback(struct fw_iso_context *context, u32 cycle,
	     size_t header_length, void *header, void *data)
{
	struct client *client = data;
	struct iso_interrupt *irq;

	irq = kzalloc(sizeof(*irq) + header_length, GFP_ATOMIC);
	if (irq == NULL)
		return;

	irq->interrupt.type      = FW_CDEV_EVENT_ISO_INTERRUPT;
	irq->interrupt.closure   = client->iso_closure;
	irq->interrupt.cycle     = cycle;
	irq->interrupt.header_length = header_length;
	memcpy(irq->interrupt.header, header, header_length);
	queue_event(client, &irq->event, &irq->interrupt,
		    sizeof(irq->interrupt) + header_length, NULL, 0);
}

static int ioctl_create_iso_context(struct client *client, void *buffer)
{
	struct fw_cdev_create_iso_context *request = buffer;
	struct fw_iso_context *context;

	/* We only support one context at this time. */
	if (client->iso_context != NULL)
		return -EBUSY;

	if (request->channel > 63)
		return -EINVAL;

	switch (request->type) {
	case FW_ISO_CONTEXT_RECEIVE:
		if (request->header_size < 4 || (request->header_size & 3))
			return -EINVAL;

		break;

	case FW_ISO_CONTEXT_TRANSMIT:
		if (request->speed > SCODE_3200)
			return -EINVAL;

		break;

	default:
		return -EINVAL;
	}

	context =  fw_iso_context_create(client->device->card,
					 request->type,
					 request->channel,
					 request->speed,
					 request->header_size,
					 iso_callback, client);
	if (IS_ERR(context))
		return PTR_ERR(context);

	client->iso_closure = request->closure;
	client->iso_context = context;

	/* We only support one context at this time. */
	request->handle = 0;

	return 0;
}

/* Macros for decoding the iso packet control header. */
#define GET_PAYLOAD_LENGTH(v)	((v) & 0xffff)
#define GET_INTERRUPT(v)	(((v) >> 16) & 0x01)
#define GET_SKIP(v)		(((v) >> 17) & 0x01)
#define GET_TAG(v)		(((v) >> 18) & 0x03)
#define GET_SY(v)		(((v) >> 20) & 0x0f)
#define GET_HEADER_LENGTH(v)	(((v) >> 24) & 0xff)

static int ioctl_queue_iso(struct client *client, void *buffer)
{
	struct fw_cdev_queue_iso *request = buffer;
	struct fw_cdev_iso_packet __user *p, *end, *next;
	struct fw_iso_context *ctx = client->iso_context;
	unsigned long payload, buffer_end, header_length;
	u32 control;
	int count;
	struct {
		struct fw_iso_packet packet;
		u8 header[256];
	} u;

	if (ctx == NULL || request->handle != 0)
		return -EINVAL;

	/*
	 * If the user passes a non-NULL data pointer, has mmap()'ed
	 * the iso buffer, and the pointer points inside the buffer,
	 * we setup the payload pointers accordingly.  Otherwise we
	 * set them both to 0, which will still let packets with
	 * payload_length == 0 through.  In other words, if no packets
	 * use the indirect payload, the iso buffer need not be mapped
	 * and the request->data pointer is ignored.
	 */

	payload = (unsigned long)request->data - client->vm_start;
	buffer_end = client->buffer.page_count << PAGE_SHIFT;
	if (request->data == 0 || client->buffer.pages == NULL ||
	    payload >= buffer_end) {
		payload = 0;
		buffer_end = 0;
	}

	p = (struct fw_cdev_iso_packet __user *)u64_to_uptr(request->packets);

	if (!access_ok(VERIFY_READ, p, request->size))
		return -EFAULT;

	end = (void __user *)p + request->size;
	count = 0;
	while (p < end) {
		if (get_user(control, &p->control))
			return -EFAULT;
		u.packet.payload_length = GET_PAYLOAD_LENGTH(control);
		u.packet.interrupt = GET_INTERRUPT(control);
		u.packet.skip = GET_SKIP(control);
		u.packet.tag = GET_TAG(control);
		u.packet.sy = GET_SY(control);
		u.packet.header_length = GET_HEADER_LENGTH(control);

		if (ctx->type == FW_ISO_CONTEXT_TRANSMIT) {
			header_length = u.packet.header_length;
		} else {
			/*
			 * We require that header_length is a multiple of
			 * the fixed header size, ctx->header_size.
			 */
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
		if (payload + u.packet.payload_length > buffer_end)
			return -EINVAL;

		if (fw_iso_context_queue(ctx, &u.packet,
					 &client->buffer, payload))
			break;

		p = next;
		payload += u.packet.payload_length;
		count++;
	}

	request->size    -= uptr_to_u64(p) - request->packets;
	request->packets  = uptr_to_u64(p);
	request->data     = client->vm_start + payload;

	return count;
}

static int ioctl_start_iso(struct client *client, void *buffer)
{
	struct fw_cdev_start_iso *request = buffer;

	if (client->iso_context == NULL || request->handle != 0)
		return -EINVAL;

	if (client->iso_context->type == FW_ISO_CONTEXT_RECEIVE) {
		if (request->tags == 0 || request->tags > 15)
			return -EINVAL;

		if (request->sync > 15)
			return -EINVAL;
	}

	return fw_iso_context_start(client->iso_context, request->cycle,
				    request->sync, request->tags);
}

static int ioctl_stop_iso(struct client *client, void *buffer)
{
	struct fw_cdev_stop_iso *request = buffer;

	if (client->iso_context == NULL || request->handle != 0)
		return -EINVAL;

	return fw_iso_context_stop(client->iso_context);
}

static int ioctl_get_cycle_timer(struct client *client, void *buffer)
{
	struct fw_cdev_get_cycle_timer *request = buffer;
	struct fw_card *card = client->device->card;
	unsigned long long bus_time;
	struct timeval tv;
	unsigned long flags;

	preempt_disable();
	local_irq_save(flags);

	bus_time = card->driver->get_bus_time(card);
	do_gettimeofday(&tv);

	local_irq_restore(flags);
	preempt_enable();

	request->local_time = tv.tv_sec * 1000000ULL + tv.tv_usec;
	request->cycle_timer = bus_time & 0xffffffff;
	return 0;
}

static int (* const ioctl_handlers[])(struct client *client, void *buffer) = {
	ioctl_get_info,
	ioctl_send_request,
	ioctl_allocate,
	ioctl_deallocate,
	ioctl_send_response,
	ioctl_initiate_bus_reset,
	ioctl_add_descriptor,
	ioctl_remove_descriptor,
	ioctl_create_iso_context,
	ioctl_queue_iso,
	ioctl_start_iso,
	ioctl_stop_iso,
	ioctl_get_cycle_timer,
};

static int
dispatch_ioctl(struct client *client, unsigned int cmd, void __user *arg)
{
	char buffer[256];
	int ret;

	if (_IOC_TYPE(cmd) != '#' ||
	    _IOC_NR(cmd) >= ARRAY_SIZE(ioctl_handlers))
		return -EINVAL;

	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		if (_IOC_SIZE(cmd) > sizeof(buffer) ||
		    copy_from_user(buffer, arg, _IOC_SIZE(cmd)))
			return -EFAULT;
	}

	ret = ioctl_handlers[_IOC_NR(cmd)](client, buffer);
	if (ret < 0)
		return ret;

	if (_IOC_DIR(cmd) & _IOC_READ) {
		if (_IOC_SIZE(cmd) > sizeof(buffer) ||
		    copy_to_user(arg, buffer, _IOC_SIZE(cmd)))
			return -EFAULT;
	}

	return ret;
}

static long
fw_device_op_ioctl(struct file *file,
		   unsigned int cmd, unsigned long arg)
{
	struct client *client = file->private_data;

	if (fw_device_is_shutdown(client->device))
		return -ENODEV;

	return dispatch_ioctl(client, cmd, (void __user *) arg);
}

#ifdef CONFIG_COMPAT
static long
fw_device_op_compat_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	struct client *client = file->private_data;

	if (fw_device_is_shutdown(client->device))
		return -ENODEV;

	return dispatch_ioctl(client, cmd, compat_ptr(arg));
}
#endif

static int fw_device_op_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct client *client = file->private_data;
	enum dma_data_direction direction;
	unsigned long size;
	int page_count, ret;

	if (fw_device_is_shutdown(client->device))
		return -ENODEV;

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

	ret = fw_iso_buffer_init(&client->buffer, client->device->card,
				 page_count, direction);
	if (ret < 0)
		return ret;

	ret = fw_iso_buffer_map(&client->buffer, vma);
	if (ret < 0)
		fw_iso_buffer_destroy(&client->buffer, client->device->card);

	return ret;
}

static int shutdown_resource(int id, void *p, void *data)
{
	struct client_resource *r = p;
	struct client *client = data;

	r->release(client, r);

	return 0;
}

static int fw_device_op_release(struct inode *inode, struct file *file)
{
	struct client *client = file->private_data;
	struct event *e, *next_e;
	unsigned long flags;

	mutex_lock(&client->device->client_list_mutex);
	list_del(&client->link);
	mutex_unlock(&client->device->client_list_mutex);

	if (client->buffer.pages)
		fw_iso_buffer_destroy(&client->buffer, client->device->card);

	if (client->iso_context)
		fw_iso_context_destroy(client->iso_context);

	/* Freeze client->resource_idr and client->event_list */
	spin_lock_irqsave(&client->lock, flags);
	client->in_shutdown = true;
	spin_unlock_irqrestore(&client->lock, flags);

	idr_for_each(&client->resource_idr, shutdown_resource, client);
	idr_remove_all(&client->resource_idr);
	idr_destroy(&client->resource_idr);

	list_for_each_entry_safe(e, next_e, &client->event_list, link)
		kfree(e);

	/*
	 * FIXME: client should be reference-counted.  It's extremely unlikely
	 * but there may still be transactions being completed at this point.
	 */
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
