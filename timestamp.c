#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

struct params {
	unsigned int syt_interval;
	unsigned int nominal_tick_gap;
	unsigned int normalized_ticks_per_event;
};

static void calculate_timestamp(struct params *p)
{
	unsigned int cycles;

	unsigned int accumulate;
	unsigned int offsets;

	uint32_t sph;
	unsigned int syt, previous_syt;

	unsigned int total;
	unsigned int data_blocks;

	accumulate = 0;
	offsets = 0;

	sph = 0;
	syt = 0;
	previous_syt = 0xffff;

	total = 0;
	cycles = 0;

	printf("db  tstamp  syt\n");

	while (cycles < 8000) {
		data_blocks = 0;
		while (1) {
			sph = (cycles << 13) | offsets;
			data_blocks++;
			if (total++ % p->syt_interval == 0) {
				syt = (((cycles & 0x7) << 13) | offsets);
				printf("    %08x *  \n", sph);
			} else {
				printf("    %08x\n", sph);
			}

			accumulate += p->nominal_tick_gap;
			if (accumulate >= 441) {
				accumulate -= 441;
				offsets++;
			}

			offsets += p->normalized_ticks_per_event;
			if (offsets >= 3072) {
				offsets -= 3072;
				break;
			}
		}

		if (previous_syt == syt)
			previous_syt = 0xffff;
		else
			previous_syt = syt;
		printf("%02d          %04x\n", data_blocks, previous_syt);

		cycles++;
	}

	printf("total: %d\n", total);
}

int main(int argc, char *argv[])
{
	static const struct {
		unsigned int syt_interval;
		unsigned int normalized_ticks_per_event;
		unsigned int nominal_tick_gap;
	} initial_state[] = {
		[0]  = {  8, 768,   0 },	/*  32000 */
		[1]  = {  8, 557, 123 },	/*  44100 */
		[2]  = {  8, 512,   0 },	/*  48000 */
		[3]  = { 16, 278, 282 },	/*  88200 */
		[4]  = { 16, 256,   0 },	/*  96000 */
		[5] =  { 32, 139, 141 },	/* 176400 */
		[6] =  { 32, 128,   0 },	/* 192000 */
	};
	static struct params p = {0};
	unsigned long sfc;

	if (argc < 2) {
		printf("./timestamp SFC\n");
		printf("    32,000: 0\n");
		printf("    44,100: 1\n");
		printf("    48,000: 2\n");
		printf("    88,200: 3\n");
		printf("    96,000: 4\n");
		printf("   176,400: 5\n");
		printf("   192,000: 6\n");
		return EXIT_FAILURE;
	}

	sfc = strtoul(argv[1], NULL, 10);

	p.syt_interval = initial_state[sfc].syt_interval;
	p.normalized_ticks_per_event =
				initial_state[sfc].normalized_ticks_per_event;
	p.nominal_tick_gap = initial_state[sfc].nominal_tick_gap;

	calculate_timestamp(&p);
	return EXIT_SUCCESS;
}
