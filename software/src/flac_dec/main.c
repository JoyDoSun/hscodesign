#include <alt_types.h>
//#include "sys/alt_stdio.h"
//#include "sys/alt_irq.h"
#include "sys/alt_flash.h"
#include <io.h>
#include <system.h>
#include <malloc.h>
#include <ctype.h>
#include "flac_dec.h"
#include "flac_dsp.h"

//#define printf printf
#define fprintf(shit, fmt, ...) printf(fmt, ##__VA_ARGS__)

#include <stdio.h>
//#include <stdlib.h>
#include <stdint.h>
#include <string.h>
//#include <errno.h>

#include "../adc128s022.h"

#include "Altera_UP_SD_Card_Avalon_Interface.h"
#include "altera_up_avalon_audio.h"
#include "altera_up_avalon_audio_and_video_config.h"

#define READ_BLOCK 17

#define SD_CARD_STATUS		0x0234
#define SD_CARD_ARGUMENT	0x022C
#define SD_CARD_COMMAND		0x0230

static int32_t decoded_l[4608], decoded_r[4608];

static uint32_t sdcard_buf[512 / 4];

alt_up_sd_card_dev *sd_card_dev;
static int *sdcard_com_arg_reg;
static short int *sdcard_com_reg;
static short int *sdcard_aux_stat_reg;

alt_up_audio_dev *audio_dev;

static int decode_main();
static int decode_frame(GetBitContext *);

static void read_block(GetBitContext *gb)
{
    int i;
    
    while (IORD_16DIRECT(sdcard_aux_stat_reg, 0) & 0x04);

    // swap bytes
    for (i = 0; i < 128; i++) {
        gb->buf[i] = ((int32_t *)sd_card_dev->base)[i];
        gb->buf[i] = (gb->buf[i]<<24) | (gb->buf[i]<<8&0x00FF0000) | (gb->buf[i]>>8&0x0000FF00) | (gb->buf[i]>>24);
    }
    
    *sdcard_com_arg_reg = ++gb->blk_cnt * 512;
	*sdcard_com_reg = READ_BLOCK;

    gb->pos -= 4096;
}

int main()
{
	sd_card_dev = alt_up_sd_card_open_dev(SD_CARD_INTERFACE_NAME);
	
	sdcard_com_arg_reg = ((int *)(sd_card_dev->base + SD_CARD_ARGUMENT));
	sdcard_com_reg = ((short int *)(sd_card_dev->base + SD_CARD_COMMAND));
	sdcard_aux_stat_reg = ((short int *)(sd_card_dev->base + SD_CARD_STATUS));
	
	audio_dev = alt_up_audio_open_dev(AUDIO_CHIP_NAME);
	
	if (audio_dev == NULL) {
	    printf("audio dev could not be opened\n");
	    return 1;
	}
	
	//while ((IORD_16DIRECT(sdcard_aux_stat_reg, 0) & 0x02) == 0);
		
	printf("sd card connected\n");
	
	*sdcard_com_arg_reg = 0;
	*sdcard_com_reg = READ_BLOCK;
  
  	decode_main();
	
	return 0;
}

static int decode_main()
{
	uint32_t buf;
    int len;

    GetBitContext gb;
    gb.read_block = read_block;
    gb.blk_cnt = 0;
    gb.buf = sdcard_buf;
    gb.pos = 4096;

    buf = get_bits(&gb, 32);

    if (strncmp((char *)&buf, "CaLf", 4)) {
        fprintf(stderr, "error: wrong file type\n");
        exit(1);
    }

    do {
        buf = get_bits1(&gb);
        skip_bits(&gb, 7);

        len = get_bits(&gb, 24);
        printf("reading metadata block of size %d%s\n", len, buf ? ", last block" : "");

        skip_bits(&gb, len * 8);
    }
    while (!buf);

    while (decode_frame(&gb) == 0);

    return 0;
}

static int decode_frame(GetBitContext *gb)
{
    int ret, l_cnt, r_cnt;
    FLACFrameInfo fi;

    if ((ret = ff_flac_decode_frame_header(gb, &fi)) < 0) {
        fprintf(stderr, "invalid frame header\n");
        return ret;
    }
    
    if (fi.blocksize > 4608) {
    	fprintf(stderr, "invalid block size (must be <= 4608, current is: %d)\n", fi.blocksize);
    	return -1;
    }
    
    if (fi.channels == 0 || fi.channels > 2) {
    	fprintf(stderr, "error: invalid channel count (must be 1 or 2, current is %d)\n", fi.channels);
    	return -1;
    }
    
    if ((ret = decode_subframe(gb, &fi, decoded_l, 0)) < 0)
    	return ret;
    
    if (fi.channels == 2)
    	if ((ret = decode_subframe(gb, &fi, decoded_r, 1)) < 0)
    		return ret;

    align_get_bits(gb);

    /* frame footer */
    skip_bits(gb, 16); /* data crc */

    switch (fi.ch_mode) {
        case FLAC_CHMODE_INDEPENDENT: flac_decorrelate_indep_c(decoded_l, decoded_r, fi.channels, fi.blocksize); break;
        case FLAC_CHMODE_LEFT_SIDE:   flac_decorrelate_ls_c(decoded_l, decoded_r, fi.channels, fi.blocksize); break;
        case FLAC_CHMODE_RIGHT_SIDE:  flac_decorrelate_rs_c(decoded_l, decoded_r, fi.channels, fi.blocksize); break;
        case FLAC_CHMODE_MID_SIDE:    flac_decorrelate_ms_c(decoded_l, decoded_r, fi.channels, fi.blocksize); break;
    }

    l_cnt = r_cnt = 0;
  			
	while (l_cnt < fi.blocksize || r_cnt < fi.blocksize) {
		if (l_cnt < fi.blocksize)
			l_cnt += alt_up_audio_write_fifo(audio_dev, (unsigned int *)(&decoded_l[l_cnt]), fi.blocksize - l_cnt, ALT_UP_AUDIO_LEFT);
		
		if (r_cnt < fi.blocksize)
			r_cnt += alt_up_audio_write_fifo(audio_dev, (unsigned int *)(&decoded_r[r_cnt]), fi.blocksize - r_cnt, ALT_UP_AUDIO_RIGHT);
	}

    return 0;
}


