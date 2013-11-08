/* monitor.c: program to monitor the status of firewire sound devices */

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <endian.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <linux/firewire-cdev.h>
#include <linux/firewire-constants.h>
#include "./include/uapi/sound/firewire.h"

static struct snd_firewire_get_info hwdep_info;
bool try_lock;

static void print_event_lock(struct snd_firewire_event_lock_status *msg, int fd)
{
	printf("Lock Status:\n");
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
	printf("Dice Norification:\n");
	printf("Notification: 0x%x\n", msg->notification);
	return;
}

static int read_event(int fd)
{
	int count;
	char buf[1024];
	struct snd_firewire_event_common *event;

	count = read(fd, buf, 1024);
	if (count < 0)
		return -1;

	event = (struct snd_firewire_event_common *)buf;
	if (event->type = SNDRV_FIREWIRE_EVENT_LOCK_STATUS)
		print_event_lock((struct snd_firewire_event_lock_status *)buf, fd);
	else if (event->type == SNDRV_FIREWIRE_EVENT_DICE_NOTIFICATION)
		print_event_dice((struct snd_firewire_event_dice_notification *)buf);

	return 0;
}

#define EVENTS 10
static int main_loop(int fd)
{
	int epfd, count, i, err;
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
			if (read_event(events[i].data.fd) < 0) {
				err = errno;
				break;
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

	err = main_loop(fd);
	close(fd);
	return err;
}
