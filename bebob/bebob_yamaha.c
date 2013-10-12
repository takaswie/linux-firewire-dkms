#include "./bebob.h"

static int
detect_dig_in(struct snd_bebob *bebob, int *detect)
{
	int err;
	u8 *buf;

	buf = kmalloc(12, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	/* This is a vendor dependent command */
	buf[0]  = 0x01;
	buf[1]  = 0xff;
	buf[2]  = 0x00;
	buf[3]  = 0x00;
	buf[4]  = 0x07;
	buf[5]  = 0xf5;
	buf[6]  = 0x00;
	buf[7]  = 0x00;
	buf[8]  = 0x01;
	buf[9]  = 0x00;
	buf[10] = 0x00;
	buf[11] = 0x00;

	err = fcp_avc_transaction(bebob->unit, buf, 12, buf, 12, 0);
	if (err < 0)
		goto end;
	/* IMPLEMENTED/STABLE is OK */
	if ((err < 6) || (buf[0] != 0x0c)){
		dev_err(&bebob->unit->device,
			"failed to detect clock source 0x%02X\n",
			buf[0]);
		err = -EIO;
		goto end;
	}

	/* when digital clock input exists, 10th byte is 0x01 */
	*detect = (buf[9] > 0);
	err = 0;	
end:
	return err;
}

static char* clock_labels[] = {"Internal", "SPDIF"};

static int clock_set(struct snd_bebob *bebob, int id)
{
	int err, detect;

	if (id > 0) {
		err = detect_dig_in(bebob, &detect);
		if ((err < 0) || (detect == 0)) {
			err = -EIO;
			goto end;
		}
	}

	spin_lock(&bebob->lock);
	err = avc_audio_set_selector(bebob->unit, 0, 4, id);
	/*
	 * Yamaha BeBob returns 'IN TRANSITION' status just after returning to
	 * internal clock
	 */
	if (id == 0)
		msleep(1500);
	spin_unlock(&bebob->lock);

end:
	return err;
}

static int clock_get(struct snd_bebob *bebob, int *id)
{
	int err;

	spin_lock(&bebob->lock);
	err = avc_audio_get_selector(bebob->unit, 0, 4, id);
	spin_unlock(&bebob->lock);

	return err;
}

static struct snd_bebob_clock_spec clock_spec = {
	.num	= sizeof(clock_labels),
	.labels	= clock_labels,
	.get	= clock_get,
	.set	= clock_set
};
struct snd_bebob_spec yamaha_go_spec = {
	.load		= NULL,
	.discover	= &snd_bebob_stream_discover,
	.map		= &snd_bebob_stream_map,
	.clock		= &clock_spec,
	.dig_iface	= NULL,
	.meter		= NULL
};
