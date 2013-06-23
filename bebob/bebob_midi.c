#include "./bebob.h"

/* TODO: We can't know the exact number of midi ports after stream starting. */
void snd_bebob_create_midi_devices(struct snd_bebob *bebob)
{
	amdtp_stream_set_midi(&bebob->receive_stream, 1);
	amdtp_stream_set_midi(&bebob->transmit_stream, 1);
}
