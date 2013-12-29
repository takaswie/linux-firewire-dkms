/* monitor.c: program to monitor the status of firewire sound devices */

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <endian.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include "./include/uapi/sound/firewire.h"

static struct snd_firewire_get_info hwdep_info;
bool try_lock;
bool fireworks = false;

static void print_event_lock(struct snd_firewire_event_lock_status *msg, int fd)
{
	printf("\nLock Status:\n");
	printf("Status:\t%s\n", msg->status ? "Locked" : "Unlocked" );

	if (try_lock) {
		if (ioctl(fd, SNDRV_FIREWIRE_IOCTL_LOCK) < 0)
			printf("lock failed\n");
		printf("lock success\n");
	}

	return;
}

static void print_event_dice(struct snd_firewire_event_dice_notification *msg)
{
	printf("\nDice Norification:\n");
	printf("Notification: 0x%x\n", msg->notification);
	return;
}

static void print_event_efw(struct snd_firewire_event_efw_response *evt, int count)
{
	uint32_t *resp;
	struct snd_efw_transaction *t;
	unsigned int i, index, length, params;

	resp = evt->response;
	count /= 4;

	index = 0;
	while (count > 0) {
		t = (struct snd_efw_transaction *)resp;
		length = be32toh(t->length);

		printf("\nEFW Response %d:\n", index++);
		printf("Length:\t\t%d\n", be32toh(t->length));
		printf("Version:\t%d\n", be32toh(t->version));
		printf("Seqnum:\t\t%d\n", be32toh(t->seqnum));
		printf("Category:\t%d\n", be32toh(t->category));
		printf("Command:\t%d\n", be32toh(t->command));
		printf("Status:\t\t%d\n", be32toh(t->status));

		params = length - sizeof(struct snd_efw_transaction) / sizeof(uint32_t);
		if (params > 0)
			for (i = 0; i < params; i++)
				printf("params[%d]:\t%08X\n", i, be32toh(t->params[i]));

		resp += length;
		count -= length * sizeof(uint32_t);
	}
}

static int read_event(int fd)
{
	int count;
	char buf[1024] = {0};
	struct snd_firewire_event_common *event;

	count = read(fd, buf, 1024);
	if (count < 0)
		return -1;

	event = (struct snd_firewire_event_common *)buf;
	if (event->type == SNDRV_FIREWIRE_EVENT_LOCK_STATUS)
		print_event_lock((struct snd_firewire_event_lock_status *)buf, fd);
	else if (event->type == SNDRV_FIREWIRE_EVENT_DICE_NOTIFICATION)
		print_event_dice((struct snd_firewire_event_dice_notification *)buf);
	else if (event->type == SNDRV_FIREWIRE_EVENT_EFW_RESPONSE)
		print_event_efw((struct snd_firewire_event_efw_response *)buf, count);

	return 0;
}

static int write_event(int fd)
{
	int count;
	struct snd_efw_transaction *t;
	char buf[1024] = {0};

	t = (struct snd_efw_transaction *)buf;
	t->length	= htobe32(6);
	t->version	= htobe32(1);
	t->seqnum	= htobe32(0);
	t->category	= htobe32(3);
	t->command	= htobe32(5);
	t->status	= htobe32(0);

	count = write(fd, buf, sizeof(struct snd_efw_transaction));
	if (count < 0)
		printf("err: %d\n", count);

	return count;
}

#define EVENTS 10
time_t cmd_deffer = 0;
static int main_loop(int fd)
{
	int epfd, count, i, err;
	time_t now;
	struct epoll_event ev, events[EVENTS];

	epfd = epoll_create(EVENTS);
	if (epfd < 0) {
		printf("error: %s\n", strerror(errno));
		err = errno;
		goto end;
	}

	memset(&ev, 0, sizeof(struct epoll_event));
	ev.events = EPOLLIN;
	ev.data.fd = fd;

	if (fireworks)
		ev.events |= EPOLLOUT;

	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) < 0)
		return errno;

	while (1) {
		count = epoll_wait(epfd, events, EVENTS, 200);
		if (count == 0)
			continue;
		else if (count < 0) {
			err = errno;
			break;
		}

		for (i = 0; i < count; i++) {
			if (events[i].events & EPOLLOUT) {
				if (time(&now) < 0)
					break;
				if (now > cmd_deffer) {
					if (write_event(events[i].data.fd) < 0) {
						err = errno;
						break;
					}
					cmd_deffer = now + 3;
				}
			}
			if (events[i].events & EPOLLIN) {
				if (read_event(events[i].data.fd) < 0) {
					err = errno;
					break;
				}
			}
		}
	}
end:
	close(epfd);
	return err;
}

int main(int argc, void *argv[])
{
	int fd, count, err;
	char buf[1024] = {};
	struct snd_firewire_event_common *event;

	if (argc > 2)
		try_lock = true;

	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		printf("fail to open: %s\n", (char *)argv[1]);
		return 1;
	}

	if (ioctl(fd, SNDRV_FIREWIRE_IOCTL_GET_INFO, &hwdep_info) < 0) {
		printf("error: %s\n", strerror(errno));
		return 1;
	}

	printf("Information of Firewire Sound Device\n");
	printf("type: %d\n", hwdep_info.type);
	printf("card: %d\n", hwdep_info.card);
	printf("GUID: 0x%02x%02x%02x%02x%02x%02x%02x%02x\n",
	       hwdep_info.guid[0], hwdep_info.guid[1], hwdep_info.guid[2],
	       hwdep_info.guid[3], hwdep_info.guid[4], hwdep_info.guid[5],
	       hwdep_info.guid[6], hwdep_info.guid[7] );
	printf("Name: %s\n\n", hwdep_info.device_name);

	if (hwdep_info.type == SNDRV_FIREWIRE_TYPE_FIREWORKS)
		fireworks = true;

	err = main_loop(fd);
	close(fd);
	return err;
}
