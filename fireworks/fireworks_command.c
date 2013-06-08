/*
 * fireworks_command.c - driver for Firewire devices from Echo Digital Audio
 *
 * Copyright (c) 2013 Takashi Sakamoto <o-takashi@sakmocchi.jp>
 *
 *
 * This driver is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.
 *
 * This driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this driver; if not, see <http://www.gnu.org/licenses/>.
 *
 * Mostly based on FFADO's souce, which is licensed under GPL version 2 (and
 * optionally version 3.
 */

#include "../lib.h"
#include "./fireworks.h"


/*
 * Echo's Fireworks(TM) utilize its own command.
 * This module calls it as 'Echo Fireworks Commands' (a.k.a EFC).
 *
 * EFC substance:
 *  At first, 6 data exist. we call these data as 'EFC fields'.
 *  Following to the 6 data, parameters for each commands exists.
 *  Most of parameters are 32 bit. But exception exists according to command.
 *   data[0]:	Length of EFC substance.
 *   data[1]:	EFC version
 *   data[2]:	Sequence number. This is incremented by both host and target
 *   data[3]:	EFC category
 *   data[4]:	EFC command
 *   data[5]:	EFC return value in EFC response.
 *   data[6-]:	parameters
 *
 * EFC address:
 *  command:	0xecc000000000
 *  response:	0xecc080000000
 *
 * As a result, Echo's Fireworks doesn't need AVC generic command sets.
 *
 * NOTE: old FFADO implementaion is EFC over AVC but device with firmware
 * version 5.5 or later don't use it but support it. This module support a part
 * of commands. Please see FFADO if you want to see whole commands.
 */

struct efc_fields {
	u32 length;
	u32 version;
	u32 seqnum;
	u32 category;
	u32 command;
	u32 retval;
	u32 params[0];
};
#define EFC_HEADER_QUADLETS	6
#define EFC_SEQNUM_MAX		(1 << 31)	/* prevent over flow */

/* for clock source and sampling rate */
struct efc_clock {
	u32 source;
	u32 sampling_rate;
	u32 index;
};

/* command categories */
enum efc_category {
	EFC_CAT_HWINFO			= 0,
	EFC_CAT_HWCTL			= 3,
	EFC_CAT_IOCONF			= 9,
};

/* hardware info category commands */
enum efc_cmd_hwinfo {
	EFC_CMD_HWINFO_GET_CAPS			= 0,
	EFC_CMD_HWINFO_GET_POLLED		= 1,
};

/* hardware control category commands */
enum efc_cmd_hwctl {
	EFC_CMD_HWCTL_SET_CLOCK		= 0,
	EFC_CMD_HWCTL_GET_CLOCK		= 1,
	EFC_CMD_HWCTL_CHANGE_FLAGS	= 3,
	EFC_CMD_HWCTL_GET_FLAGS		= 4,
};
/* for flags */
#define EFC_HWCTL_FLAG_DIGITAL_PRO	0x02
#define EFC_HWCTL_FLAG_DIGITAL_RAW	0x04

/* I/O config category commands */
enum efc_cmd_ioconf {
	EFC_CMD_IOCONF_SET_DIGITAL_MODE	= 2,
	EFC_CMD_IOCONF_GET_DIGITAL_MODE	= 3,
};

/* return values in response */
enum efc_retval {
	EFC_RETVAL_OK			= 0,
	EFC_RETVAL_BAD			= 1,
	EFC_RETVAL_BAD_COMMAND		= 2,
	EFC_RETVAL_COMM_ERR		= 3,
	EFC_RETVAL_BAD_QUAD_COUNT	= 4,
	EFC_RETVAL_UNSUPPORTED		= 5,
	EFC_RETVAL_1394_TIMEOUT		= 6,
	EFC_RETVAL_DSP_TIMEOUT		= 7,
	EFC_RETVAL_BAD_RATE		= 8,
	EFC_RETVAL_BAD_CLOCK		= 9,
	EFC_RETVAL_BAD_CHANNEL		= 10,
	EFC_RETVAL_BAD_PAN		= 11,
	EFC_RETVAL_FLASH_BUSY		= 12,
	EFC_RETVAL_BAD_MIRROR		= 13,
	EFC_RETVAL_BAD_LED		= 14,
	EFC_RETVAL_BAD_PARAMETER	= 15,
	EFC_RETVAL_INCOMPLETE		= 0x80000000
};

static const char *const efc_retval_names[] = {
	[EFC_RETVAL_OK]			= "OK",
	[EFC_RETVAL_BAD]		= "bad",
	[EFC_RETVAL_BAD_COMMAND]	= "bad command",
	[EFC_RETVAL_COMM_ERR]		= "comm err",
	[EFC_RETVAL_BAD_QUAD_COUNT]	= "bad quad count",
	[EFC_RETVAL_UNSUPPORTED]	= "unsupported",
	[EFC_RETVAL_1394_TIMEOUT]	= "1394 timeout",
	[EFC_RETVAL_DSP_TIMEOUT]	= "DSP timeout",
	[EFC_RETVAL_BAD_RATE]		= "bad rate",
	[EFC_RETVAL_BAD_CLOCK]		= "bad clock",
	[EFC_RETVAL_BAD_CHANNEL]	= "bad channel",
	[EFC_RETVAL_BAD_PAN]		= "bad pan",
	[EFC_RETVAL_FLASH_BUSY]		= "flash busy",
	[EFC_RETVAL_BAD_MIRROR]		= "bad mirror",
	[EFC_RETVAL_BAD_LED]		= "bad LED",
	[EFC_RETVAL_BAD_PARAMETER]	= "bad parameter",
	[EFC_RETVAL_BAD_PARAMETER + 1]	= "incomplete"
};

static int
efc_transaction_run(struct fw_unit *unit,
		    const void *command, unsigned int command_size,
		    void *response, unsigned int size,
		    u32 seqnum);

static int
efc(struct snd_efw *efw, unsigned int category,
    unsigned int command,
    const u32 *params, unsigned int param_count,
    void *response, unsigned int response_quadlets)
{
	int err;

	unsigned int cmdbuf_bytes;
	__be32 *cmdbuf;
	struct efc_fields *efc_fields;
	u32 seqnum;
	unsigned int i;

	/* calculate buffer size*/
	cmdbuf_bytes = EFC_HEADER_QUADLETS * 4
			 + max(param_count, response_quadlets) * 4;

	/* keep buffer */
	cmdbuf = kzalloc(cmdbuf_bytes, GFP_KERNEL);
	if (cmdbuf == NULL)
		return -ENOMEM;

	/* to keep consistency of sequence number */
	spin_lock(&efw->lock);
	seqnum = efw->seqnum;
	if (efw->seqnum > EFC_SEQNUM_MAX)
		efw->seqnum = 0;
	else
		efw->seqnum += 2;
	spin_unlock(&efw->lock);

	/* fill efc fields */
	efc_fields		= (struct efc_fields *)cmdbuf;
	efc_fields->length	= EFC_HEADER_QUADLETS + param_count;
	efc_fields->version	= 1;
	efc_fields->seqnum	= seqnum;
	efc_fields->category	= category;
	efc_fields->command	= command;
	efc_fields->retval	= 0;

	/* fill EFC parameters */
	for (i = 0; i < param_count; i++)
		efc_fields->params[i] = params[i];

	/* for endian-ness*/
	for (i = 0; i < (cmdbuf_bytes / 4); i++)
		cmdbuf[i] = cpu_to_be32(cmdbuf[i]);

	/* if return value is positive, it means return bytes */
	/* TODO: the last parameter should be sequence number */
	err = efc_transaction_run(efw->unit, cmdbuf, cmdbuf_bytes,
				  cmdbuf, cmdbuf_bytes, seqnum);
	if (err < 0)
		goto end;

	/* for endian-ness */
	for (i = 0; i < (err / 4); i += 1)
		cmdbuf[i] = be32_to_cpu(cmdbuf[i]);

	/* check EFC response fields */
	if ((efc_fields->version < 1) ||
	    (efc_fields->category != category) ||
	    (efc_fields->command != command) ||
	    (efc_fields->retval != EFC_RETVAL_OK)) {
		dev_err(&efw->unit->device, "EFC failed [%u/%u]: %s\n",
			efc_fields->category, efc_fields->command,
			efc_retval_names[efc_fields->retval]);
		err = -EIO;
		goto end;
	}

	/* fill response buffer */
	if (response != NULL) {
		memset(response, 0, response_quadlets * 4);
		response_quadlets = min(response_quadlets, efc_fields->length);
		memcpy(response, efc_fields->params, response_quadlets * 4);
	}

	err = 0;

end:
	kfree(cmdbuf);
	return err;
}

int snd_efw_command_get_hwinfo(struct snd_efw *efw,
			       struct snd_efw_hwinfo *hwinfo)
{
	u32 *tmp;
	int i;
	int count;
	int err = efc(efw, EFC_CAT_HWINFO,
				EFC_CMD_HWINFO_GET_CAPS,
				NULL, 0, hwinfo, sizeof(*hwinfo) / 4);
	if (err < 0)
		goto end;

	/* arrangement for endianness */
	count = HWINFO_NAME_SIZE_BYTES / 4;
	tmp = (u32 *)&hwinfo->vendor_name;
	for (i = 0; i < count; i += 1)
		tmp[i] = cpu_to_be32(tmp[i]);
	tmp = (u32 *)&hwinfo->model_name;
	for (i = 0; i < count; i += 1)
		tmp[i] = cpu_to_be32(tmp[i]);

	count = sizeof(struct snd_efw_phys_group) * HWINFO_MAX_CAPS_GROUPS / 4;
	tmp = (u32 *)&hwinfo->out_groups;
	for (i = 0; i < count; i += 1)
		tmp[i] = cpu_to_be32(tmp[i]);
	tmp = (u32 *)&hwinfo->in_groups;
	for (i = 0; i < count; i += 1)
		tmp[i] = cpu_to_be32(tmp[i]);

	/* ensure terminated */
	hwinfo->vendor_name[HWINFO_NAME_SIZE_BYTES - 1] = '\0';
	hwinfo->model_name[HWINFO_NAME_SIZE_BYTES  - 1] = '\0';

	err = 0;
end:
	return err;
}

int snd_efw_command_get_phys_meters(struct snd_efw *efw,
				    struct snd_efw_phys_meters *meters,
				    int len)
{
	return efc(efw, EFC_CAT_HWINFO,
				EFC_CMD_HWINFO_GET_POLLED,
				NULL, 0, meters, len / 4);
}

static int
command_get_clock(struct snd_efw *efw, struct efc_clock *clock)
{
	return efc(efw, EFC_CAT_HWCTL,
				EFC_CMD_HWCTL_GET_CLOCK,
				NULL, 0, clock, sizeof(struct efc_clock) / 4);
}

static int
command_set_clock(struct snd_efw *efw,
			  int source, int sampling_rate)
{
	int err;

	struct efc_clock clock = {0};

	/* check arguments */
	if ((source < 0) && (sampling_rate < 0)) {
		err = -EINVAL;
		goto end;
	}

	/* get current status */
	err = command_get_clock(efw, &clock);
	if (err < 0)
		goto end;

	/* no need */
	if ((clock.source == source) &&
	    (clock.sampling_rate == sampling_rate))
		goto end;

	/* set params */
	if ((source >= 0) && (clock.source != source))
		clock.source = source;
	if ((sampling_rate > 0) && (clock.sampling_rate != sampling_rate))
		clock.sampling_rate = sampling_rate;
	clock.index = 0;

	err = efc(efw, EFC_CAT_HWCTL,
				EFC_CMD_HWCTL_SET_CLOCK,
				(u32 *)&clock, 3, NULL, 0);

	err = 0;
end:
	return err;
}

int snd_efw_command_get_clock_source(struct snd_efw *efw,
				     enum snd_efw_clock_source *source)
{
	int err;
	struct efc_clock clock = {0};

	err = command_get_clock(efw, &clock);
	if (err >= 0)
		*source = clock.source;

	return err;
}

int snd_efw_command_set_clock_source(struct snd_efw *efw,
				     enum snd_efw_clock_source source)
{
	return command_set_clock(efw, source, -1);
}

int snd_efw_command_get_sampling_rate(struct snd_efw *efw,
				      int *sampling_rate)
{
	int err;
	struct efc_clock clock = {0};

	err = command_get_clock(efw, &clock);
	if (err >= 0)
		*sampling_rate = clock.sampling_rate;

	return err;
}

int
snd_efw_command_set_sampling_rate(struct snd_efw *efw, int sampling_rate)
{
	return command_set_clock(efw, -1, sampling_rate);
}

int snd_efw_command_get_iec60958_format(struct snd_efw *efw,
					enum snd_efw_iec60958_format *format)
{
	int err;
	u32 flag = {0};

	err = efc(efw, EFC_CAT_HWCTL,
				EFC_CMD_HWCTL_GET_FLAGS,
				NULL, 0, &flag, 1);
	if (err >= 0) {
		if (flag & EFC_HWCTL_FLAG_DIGITAL_PRO)
			*format = SND_EFW_IEC60958_FORMAT_PROFESSIONAL;
		else
			*format = SND_EFW_IEC60958_FORMAT_CONSUMER;
	}

	return err;
}

int snd_efw_command_set_iec60958_format(struct snd_efw *efw,
					enum snd_efw_iec60958_format format)
{
	/*
	 * mask[0]: for set
	 * mask[1]: for clear
	 */
	u32 mask[2] = {0};

	if (format == SND_EFW_IEC60958_FORMAT_PROFESSIONAL)
		mask[0] = EFC_HWCTL_FLAG_DIGITAL_PRO;
	else
		mask[1] = EFC_HWCTL_FLAG_DIGITAL_PRO;

	return efc(efw, EFC_CAT_HWCTL,
				EFC_CMD_HWCTL_CHANGE_FLAGS,
				(u32 *)mask, 2, NULL, 0);
}

int snd_efw_command_get_digital_interface(struct snd_efw *efw,
			enum snd_efw_digital_interface *digital_interface)
{
	int err;
	u32 value = 0;

	err = efc(efw, EFC_CAT_IOCONF,
				EFC_CMD_IOCONF_GET_DIGITAL_MODE,
				NULL, 0, &value, 1);

	if (err >= 0)
		*digital_interface = value;

	return err;
}

int snd_efw_command_set_digital_interface(struct snd_efw *efw,
			enum snd_efw_digital_interface digital_interface)
{
	u32 value = digital_interface;

	return efc(efw, EFC_CAT_IOCONF,
				EFC_CMD_IOCONF_SET_DIGITAL_MODE,
				&value, 1, NULL, 0);
}

#define INITIAL_MEMORY_SPACE_EFC_COMMAND	0xecc000000000
#define INITIAL_MEMORY_SPACE_EFC_RESPONSE	0xecc080000000
/* this for juju convinience */
#define INITIAL_MEMORY_SPACE_EFC_END		0xecc080000200

#define ERROR_RETRIES 3
#define ERROR_DELAY_MS 5
#define EFC_TIMEOUT_MS 125

static DEFINE_SPINLOCK(transactions_lock);
static LIST_HEAD(transactions);

enum efc_state {
	STATE_PENDING,
	STATE_BUS_RESET,
	STATE_COMPLETE
};

struct efc_transaction {
	struct list_head list;
	struct fw_unit *unit;
	void *buffer;
	unsigned int size;
	u32 seqnum;
	enum efc_state state;
	wait_queue_head_t wait;
};

static int
efc_transaction_run(struct fw_unit *unit,
		    const void *command, unsigned int command_size,
		    void *response, unsigned int size, u32 seqnum)
{
	struct efc_transaction t;
	int tcode, ret, tries = 0;

	t.unit = unit;
	t.buffer = response;
	t.size = size;
	t.seqnum = seqnum + 1;
	t.state = STATE_PENDING;
	init_waitqueue_head(&t.wait);

	spin_lock_irq(&transactions_lock);
	list_add_tail(&t.list, &transactions);
	spin_unlock_irq(&transactions_lock);

	do {
		tcode = command_size == 4 ? TCODE_WRITE_QUADLET_REQUEST
					  : TCODE_WRITE_BLOCK_REQUEST;
		ret = snd_fw_transaction(t.unit, tcode,
					 INITIAL_MEMORY_SPACE_EFC_COMMAND,
					 (void *)command, command_size);
		if (ret < 0)
			break;

		wait_event_timeout(t.wait, t.state != STATE_PENDING,
				   msecs_to_jiffies(EFC_TIMEOUT_MS));

		if (t.state == STATE_COMPLETE) {
			ret = t.size;
			break;
		} else if (t.state == STATE_BUS_RESET) {
			msleep(ERROR_DELAY_MS);
		} else if (++tries >= ERROR_RETRIES) {
			dev_err(&t.unit->device, "EFC command timed out\n");
			ret = -EIO;
			break;
		}
	} while(1);

	spin_lock_irq(&transactions_lock);
	list_del(&t.list);
	spin_unlock_irq(&transactions_lock);

	return ret;
}

static void
efc_response(struct fw_card *card, struct fw_request *request,
	     int tcode, int destination, int source,
	     int generation, unsigned long long offset,
	     void *data, size_t length, void *callback_data)
{
	struct efc_transaction *t;
	unsigned long flags;
	u32 seqnum;

	if (length < 1)
		return;

	seqnum = be32_to_cpu(((struct efc_fields *)data)->seqnum);

	spin_lock_irqsave(&transactions_lock, flags);
	list_for_each_entry(t, &transactions, list) {
		struct fw_device *device = fw_parent_device(t->unit);
		if ((device->card != card) ||
		    (device->generation != generation))
			continue;
		smp_rmb();	/* node_id vs. generation */
		if (device->node_id != source)
			continue;

		if ((t->state == STATE_PENDING) && (t->seqnum == seqnum)) {
			t->state = STATE_COMPLETE;
			t->size = min((unsigned int)length, t->size);
			memcpy(t->buffer, data, t->size);
			wake_up(&t->wait);
		}
	}
	spin_unlock_irqrestore(&transactions_lock, flags);
}

void snd_efw_command_bus_reset(struct fw_unit *unit)
{
	struct efc_transaction *t;

	spin_lock_irq(&transactions_lock);
	list_for_each_entry(t, &transactions, list) {
		if ((t->unit == unit) &&
		    (t->state == STATE_PENDING)) {
			t->state = STATE_BUS_RESET;
			wake_up(&t->wait);
		}
	}
	spin_unlock_irq(&transactions_lock);
}

static struct fw_address_handler response_register_handler = {
	/* TODO: this span should be reconsidered */
	.length = INITIAL_MEMORY_SPACE_EFC_END - INITIAL_MEMORY_SPACE_EFC_RESPONSE,
	.address_callback = efc_response
};

int snd_efw_command_create(struct snd_efw *efw)
{
	static const struct fw_address_region response_register_region = {
		.start	= INITIAL_MEMORY_SPACE_EFC_RESPONSE,
		.end	= INITIAL_MEMORY_SPACE_EFC_END
	};

	efw->seqnum = 0;

	return fw_core_add_address_handler(&response_register_handler,
					   &response_register_region);
}

void snd_efw_command_destroy(void)
{
	WARN_ON(!list_empty(&transactions));
	fw_core_remove_address_handler(&response_register_handler);
}
