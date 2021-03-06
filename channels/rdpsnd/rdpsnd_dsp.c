/*
   FreeRDP: A Remote Desktop Protocol client.
   Digital Sound Processing

   Copyright 2010-2011 Vic Lee

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <freerdp/types_ui.h>
#include "rdpsnd_types.h"
#include "rdpsnd_dsp.h"

uint8 *
rdpsnd_dsp_resample(uint8 * src, int bytes_per_sample,
	uint32 schan, uint32 srate, int sframes,
	uint32 rchan, uint32 rrate, int * prframes)
{
	uint8 * dst;
	uint8 * p;
	int rframes;
	int rsize;
	int i, j;
	int n1, n2;
	int sbytes, rbytes;

	sbytes = bytes_per_sample * schan;
	rbytes = bytes_per_sample * rchan;
	rframes = sframes * rrate / srate;
	*prframes = rframes;
	rsize = rbytes * rframes;
	dst = (uint8 *) malloc(rsize);
	memset(dst, 0, rsize);

	p = dst;
	for (i = 0; i < rframes; i++)
	{
		n1 = i * srate / rrate;
		if (n1 >= sframes)
			n1 = sframes - 1;
		n2 = (n1 * rrate == i * srate || n1 == sframes - 1 ? n1 : n1 + 1);
		for (j = 0; j < rbytes; j++)
		{
			/* Nearest Interpolation, probably the easiest, but works */
			*p++ = (i * srate - n1 * rrate > n2 * rrate - i * srate ?
				src[n2 * sbytes + (j % sbytes)] :
				src[n1 * sbytes + (j % sbytes)]);
		}
	}

	return dst;
}

/*
   Microsoft IMA ADPCM specification:

   http://wiki.multimedia.cx/index.php?title=Microsoft_IMA_ADPCM
   http://wiki.multimedia.cx/index.php?title=IMA_ADPCM
*/

static const sint16 ima_step_index_table[] =
{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8
};

static const sint16 ima_step_size_table[] =
{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 
	19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 
	130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
	337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
	876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 
	2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
	5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 
};

static uint16
rdpsnd_dsp_decode_ima_adpcm_sample(rdpsndDspAdpcm * adpcm,
	int channel, uint8 sample)
{
	sint32 ss;
	sint32 d;

	ss = ima_step_size_table[adpcm->last_step[channel]];
	d = (ss >> 3);
	if (sample & 1)
		d += (ss >> 2);
	if (sample & 2)
		d += (ss >> 1);
	if (sample & 4)
		d += ss;
	if (sample & 8)
		d = -d;
	d += adpcm->last_sample[channel];

	if (d < -32768)
		d = -32768;
	else if (d > 32767)
		d = 32767;

	adpcm->last_sample[channel] = (sint16) d;

	adpcm->last_step[channel] += ima_step_index_table[sample];
	if (adpcm->last_step[channel] < 0)
		adpcm->last_step[channel] = 0;
	else if (adpcm->last_step[channel] > 88)
		adpcm->last_step[channel] = 88;

	return (uint16) d;
}

uint8 *
rdpsnd_dsp_decode_ima_adpcm(rdpsndDspAdpcm * adpcm,
	uint8 * src, int size, int channels, int block_size, int * out_size)
{
	uint8 * out;
	uint8 * dst;
	uint8 sample;
	uint16 decoded;
	int channel;
	int i;

	*out_size = size * 4;
	out = (uint8 *) malloc(*out_size);
	dst = out;
	while (size > 0)
	{
		if (size % block_size == 0)
		{
			adpcm->last_sample[0] = (sint16) (((uint16)(*src)) | (((uint16)(*(src + 1))) << 8));
			adpcm->last_step[0] = (sint16) (*(src + 2));
			src += 4;
			size -= 4;
			*out_size -= 16;
			if (channels > 1)
			{
				adpcm->last_sample[1] = (sint16) (((uint16)(*src)) | (((uint16)(*(src + 1))) << 8));
				adpcm->last_step[1] = (sint16) (*(src + 2));
				src += 4;
				size -= 4;
				*out_size -= 16;
			}
		}

		if (channels > 1)
		{
			for (i = 0; i < 8; i++)
			{
				channel = (i < 4 ? 0 : 1);
				sample = ((*src) & 0x0f);
				decoded = rdpsnd_dsp_decode_ima_adpcm_sample(adpcm, channel, sample);
				dst[((i & 3) << 3) + (channel << 1)] = (decoded & 0xff);
				dst[((i & 3) << 3) + (channel << 1) + 1] = (decoded >> 8);
				sample = ((*src) >> 4);
				decoded = rdpsnd_dsp_decode_ima_adpcm_sample(adpcm, channel, sample);
				dst[((i & 3) << 3) + (channel << 1) + 4] = (decoded & 0xff);
				dst[((i & 3) << 3) + (channel << 1) + 5] = (decoded >> 8);
				src++;
			}
			dst += 32;
			size -= 8;
		}
		else
		{
			sample = ((*src) & 0x0f);
			decoded = rdpsnd_dsp_decode_ima_adpcm_sample(adpcm, 0, sample);
			*dst++ = (decoded & 0xff);
			*dst++ = (decoded >> 8);
			sample = ((*src) >> 4);
			decoded = rdpsnd_dsp_decode_ima_adpcm_sample(adpcm, 0, sample);
			*dst++ = (decoded & 0xff);
			*dst++ = (decoded >> 8);
			src++;
			size--;
		}
	}
	return out;
}

