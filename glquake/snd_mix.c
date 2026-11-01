/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// snd_mix.c -- portable code to mix sounds for snd_dma.c

#include "quakedef.h"

#define PAINTBUFFER_SIZE  512 // 2048 ? Cowcat 2019

portable_samplepair_t paintbuffer[PAINTBUFFER_SIZE];
int   snd_scaletable[32][256];
int   *snd_p, snd_linear_count, snd_vol;
short *snd_out;


/*
 * MiniGLV3D port note: MGLV3D_NO_ASM (new, this port only -- never
 * defined by any of glquake's own real Makefiles) selects this file's
 * portable C implementation instead of expecting snd_mixamiga68k.s to
 * supply S_TransferPaintBuffer. Scoped narrowly to this one file/symbol
 * rather than touching the real __GNUC__ check other Makefile variants
 * (WOS/PPC-gcc) actually rely on. Only meaningful for the "C fallback"
 * build variant -- the real-assembly variant assembles snd_mixamiga68k.s
 * for real and never defines this.
 *
 * GCC feasibility investigation update: the original !defined(__GNUC__)
 * here was only ever a proxy for "not a PowerPC/WarpOS GCC build" (which
 * genuinely can't run M68K asm) -- back when the only GCC variant that
 * existed for this project WAS PPC-GCC. Now that m68k-amigaos-gcc can
 * assemble the real snd_mixamiga68k_gas.s translation (byte-verified
 * against vasm elsewhere in this investigation), that proxy is wrong for
 * this new target: it would silently pull in this file's OWN C
 * implementation of S_TransferPaintBuffer too, alongside the real one
 * from the linked .o -- a genuine duplicate-symbol link error, not a
 * theoretical one (hit for real building glquake_asm_gcc_o2). Keying on
 * __PPC__ directly instead is the same fix already applied to sys_amiga.c
 * /snd_paula.c's TimerBase type elsewhere in this project, for the exact
 * same M68K-GCC-vs-PPC-GCC distinction.
 */
#if !defined(__PPC__) && !defined(MGLV3D_NO_ASM) // asm Amiga stuff

extern void S_TransferPaintBuffer(int);

#else

void Snd_WriteLinearBlastStereo16 (void)
{
        int   i;
        int   val;

        for (i=0 ; i<snd_linear_count ; i+=2)
        {
                val = (snd_p[i]*snd_vol)>>8; // maybe snd_p[i] / 256 ? - Cowcat 2019

                if (val > 0x7fff)
                        snd_out[i] = 0x7fff;

                else if (val < (short)0x8000)
                        snd_out[i] = (short)0x8000;

                else
                        snd_out[i] = val;

                val = (snd_p[i+1]*snd_vol)>>8;

                if (val > 0x7fff)
                        snd_out[i+1] = 0x7fff;

                else if (val < (short)0x8000)
                        snd_out[i+1] = (short)0x8000;

                else
                        snd_out[i+1] = val;
        }
}

void S_TransferStereo16 (int endtime)
{
        int             lpos;
        int             lpaintedtime;
        unsigned long   *pbuf;
  
        snd_vol = volume.value*256;

        snd_p = (int *) paintbuffer;
        lpaintedtime = paintedtime;
        pbuf = (unsigned long *)shm->buffer;

        while (lpaintedtime < endtime)
        {
                // handle recirculating buffer issues
                lpos = lpaintedtime & ((shm->samples>>1)-1);

                snd_out = (short *) pbuf + (lpos<<1);

                snd_linear_count = (shm->samples>>1) - lpos;

                if (lpaintedtime + snd_linear_count > endtime)
                        snd_linear_count = endtime - lpaintedtime;

                snd_linear_count <<= 1;

                // write a linear blast of samples
                Snd_WriteLinearBlastStereo16 ();

                snd_p += snd_linear_count;
                lpaintedtime += (snd_linear_count>>1);
        }
}

void S_TransferPaintBuffer(int endtime)
{
        int             out_idx;
        int             count;
        int             out_mask;
        int             *p;
        int             step;
        int             val;
        int             snd_vol;
        unsigned long   *pbuf;

        if (shm->samplebits == 16 && shm->channels == 2)
        {
                S_TransferStereo16 (endtime);
                return;
        }
  
        p = (int *) paintbuffer;
        count = (endtime - paintedtime) * shm->channels;
        out_mask = shm->samples - 1; 
        out_idx = paintedtime * shm->channels & out_mask;
        step = 3 - shm->channels;
        snd_vol = volume.value*256;
        pbuf = (unsigned long *)shm->buffer;

        if (shm->samplebits == 16)
        {
                short *out = (short *) pbuf;

                while (count--)
                {
                        val = (*p * snd_vol) >> 8; // maybe *p / 256 ? - Cowcat 2019
                        p+= step;

                        if (val > 0x7fff)
                                val = 0x7fff;

                        else if (val < (short)0x8000)
                                val = (short)0x8000;

                        out[out_idx] = val;
                        out_idx = (out_idx + 1) & out_mask;
                }
        }

        else if (shm->samplebits == 8)
        {
                //unsigned char *out = (unsigned char *) pbuf;
                signed char *out = (signed char *) pbuf; // signed Amiga - Cowcat 2019

                while (count--)
                {
                        val = (*p * snd_vol) >> 8;
                        p+= step;

                        if (val > 0x7fff)
                                val = 0x7fff;

                        else if (val < (short)0x8000)
                                val = (short)0x8000;

                        //out[out_idx] = (val>>8) + 128;
			out[out_idx] = (val / 256); // correct ? - Cowcat 2019
                        out_idx = (out_idx + 1) & out_mask;
                }
        }
}

#endif



/*
===============================================================================

CHANNEL MIXING

===============================================================================
*/

void SND_PaintChannelFrom8 (channel_t *ch, sfxcache_t *sc, int endtime, int paintbufferstart); // quakespasm paintbuffer
void SND_PaintChannelFrom16 (channel_t *ch, sfxcache_t *sc, int endtime, int paintbufferstart); //

void S_PaintChannels(int endtime)
{
        int             i;
        int             end, ltime, count;
        channel_t       *ch;
        sfxcache_t      *sc;    

        while (paintedtime < endtime)
        {
                // if paintbuffer is smaller than DMA buffer
                end = endtime;

                if (endtime - paintedtime > PAINTBUFFER_SIZE)
                        end = paintedtime + PAINTBUFFER_SIZE;

                // clear the paint buffer
                Q_memset(paintbuffer, 0, (end - paintedtime) * sizeof(portable_samplepair_t));

                // paint in the channels.
                ch = channels;

                for (i=0; i<total_channels ; i++, ch++)
                {
                        if (!ch->sfx)
                                continue;

                        if (!ch->leftvol && !ch->rightvol)
                                continue;

                        sc = S_LoadSound (ch->sfx);

                        if (!sc)
                                continue;

                        ltime = paintedtime;

                        while (ltime < end)
                        {
                                // paint up to end
                                if (ch->end < end)
                                        count = ch->end - ltime;

                                else
                                        count = end - ltime;

                                if (count > 0)
                                { 
                                        if (sc->width == 1)
                                                SND_PaintChannelFrom8(ch, sc, count, ltime - paintedtime);

                                        else
                                                SND_PaintChannelFrom16(ch, sc, count, ltime - paintedtime);
  
                                        ltime += count;
                                }

                                // if at end of loop, restart
                                if (ltime >= ch->end)
                                {
                                        if (sc->loopstart >= 0)
                                        {
                                                ch->pos = sc->loopstart;
                                                ch->end = ltime + sc->length - ch->pos;
                                        }

                                        else        
                                        {       // channel just stopped
                                                ch->sfx = NULL;
                                                break;
                                        }
                                }
                        }
                                
                }

                // transfer out according to DMA format
                S_TransferPaintBuffer(end);
                paintedtime = end;
        }
}

void SND_InitScaletable (void)
{
        int   i, j;

        for (i=0 ; i<32 ; i++)
                for (j=0 ; j<256 ; j++)
                        snd_scaletable[i][j] = ((signed char)j) * i * 8;
}


void SND_PaintChannelFrom8 (channel_t *ch, sfxcache_t *sc, int count, int paintbufferstart)
{
        int             data;
        int             *lscale, *rscale;
        unsigned char	*sfx;
        int             i;

        if (ch->leftvol > 255)
                ch->leftvol = 255;

        if (ch->rightvol > 255)
                ch->rightvol = 255;
    
        lscale = snd_scaletable[ch->leftvol >> 3];
        rscale = snd_scaletable[ch->rightvol >> 3];
        sfx = (unsigned char *)sc->data + ch->pos; // was signed char - Cowcat 2019

        for (i=0 ; i<count ; i++)
        {
                data = sfx[i];
                paintbuffer[paintbufferstart + i].left += lscale[data];
                paintbuffer[paintbufferstart + i].right += rscale[data];
        }
  
        ch->pos += count;
}


void SND_PaintChannelFrom16 (channel_t *ch, sfxcache_t *sc, int count, int paintbufferstart)
{
        int             data;
        int             left, right;
        int             leftvol, rightvol;
        signed short    *sfx;
        int             i;

        leftvol = ch->leftvol;
        rightvol = ch->rightvol;

        //leftvol >>= 8; // fail on Amiga !! - Cowcat 2019
        //rightvol >>= 8; //

        sfx = (signed short *)sc->data + ch->pos;

        for (i=0 ; i<count ; i++)
        {
                data = sfx[i];
                left = (data * leftvol) >> 8;  
                right = (data * rightvol) >> 8;
                //left = data * leftvol;	// fail on Amiga !! Cowcat 2019
                //right = data * rightvol;	// !! 
                paintbuffer[paintbufferstart + i].left += left;
                paintbuffer[paintbufferstart + i].right += right;
        }

        ch->pos += count;
}
