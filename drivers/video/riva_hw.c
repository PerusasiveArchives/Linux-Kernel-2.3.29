 /***************************************************************************\
|*                                                                           *|
|*       Copyright 1993-1998 NVIDIA, Corporation.  All rights reserved.      *|
|*                                                                           *|
|*     NOTICE TO USER:   The source code  is copyrighted under  U.S. and     *|
|*     international laws.  Users and possessors of this source code are     *|
|*     hereby granted a nonexclusive,  royalty-free copyright license to     *|
|*     use this code in individual and commercial software.                  *|
|*                                                                           *|
|*     Any use of this source code must include,  in the user documenta-     *|
|*     tion and  internal comments to the code,  notices to the end user     *|
|*     as follows:                                                           *|
|*                                                                           *|
|*       Copyright 1993-1998 NVIDIA, Corporation.  All rights reserved.      *|
|*                                                                           *|
|*     NVIDIA, CORPORATION MAKES NO REPRESENTATION ABOUT THE SUITABILITY     *|
|*     OF  THIS SOURCE  CODE  FOR ANY PURPOSE.  IT IS  PROVIDED  "AS IS"     *|
|*     WITHOUT EXPRESS OR IMPLIED WARRANTY OF ANY KIND.  NVIDIA, CORPOR-     *|
|*     ATION DISCLAIMS ALL WARRANTIES  WITH REGARD  TO THIS SOURCE CODE,     *|
|*     INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY, NONINFRINGE-     *|
|*     MENT,  AND FITNESS  FOR A PARTICULAR PURPOSE.   IN NO EVENT SHALL     *|
|*     NVIDIA, CORPORATION  BE LIABLE FOR ANY SPECIAL,  INDIRECT,  INCI-     *|
|*     DENTAL, OR CONSEQUENTIAL DAMAGES,  OR ANY DAMAGES  WHATSOEVER RE-     *|
|*     SULTING FROM LOSS OF USE,  DATA OR PROFITS,  WHETHER IN AN ACTION     *|
|*     OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,  ARISING OUT OF     *|
|*     OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOURCE CODE.     *|
|*                                                                           *|
|*     U.S. Government  End  Users.   This source code  is a "commercial     *|
|*     item,"  as that  term is  defined at  48 C.F.R. 2.101 (OCT 1995),     *|
|*     consisting  of "commercial  computer  software"  and  "commercial     *|
|*     computer  software  documentation,"  as such  terms  are  used in     *|
|*     48 C.F.R. 12.212 (SEPT 1995)  and is provided to the U.S. Govern-     *|
|*     ment only as  a commercial end item.   Consistent with  48 C.F.R.     *|
|*     12.212 and  48 C.F.R. 227.7202-1 through  227.7202-4 (JUNE 1995),     *|
|*     all U.S. Government End Users  acquire the source code  with only     *|
|*     those rights set forth herein.                                        *|
|*                                                                           *|
 \***************************************************************************/
/* 
 * GPL licensing note -- nVidia is allowing a liberal interpretation of 
 * the documentation restriction above, to merely say that this nVidia's
 * copyright and disclaimer should be included with all code derived 
 * from this source.  -- Jeff Garzik <jgarzik@mandrakesoft.com>, 01/Nov/99
 */

/* $XFree86: xc/programs/Xserver/hw/xfree86/vga256/drivers/nv/riva_hw.c,v 1.1.2.3 1998/12/26 00:12:39 dawes Exp $ */

#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include "riva_hw.h"
#include "riva_tbl.h"


/*
 * This file is an OS-agnostic file used to make RIVA 128 and RIVA TNT
 * operate identically (except TNT has more memory and better 3D quality.
 */

static int nv3Busy
(
    RIVA_HW_INST *chip
)
{
    return ((!(chip->PFIFO[0x00001214/4] & 0x10)) | (chip->PGRAPH[0x000006B0/4] & 0x01));
}
static int nv4Busy
(
    RIVA_HW_INST *chip
)
{
    return ((!(chip->PFIFO[0x00001214/4] & 0x10)) | (chip->PGRAPH[0x00000700/4] & 0x01));
}
static int ShowHideCursor
(
    RIVA_HW_INST *chip,
    int           ShowHide
)
{
    int xcurrent;
    xcurrent                     =  chip->CurrentState->cursor1;
    chip->CurrentState->cursor1 = (chip->CurrentState->cursor1 & 0xFE) | (ShowHide & 0x01);
    outb(0x31, 0x3D4);
    outb(chip->CurrentState->cursor1, 0x3D5);
    return (xcurrent & 0x01);
}

/****************************************************************************\
*                                                                            *
* The video arbitration routines calculate some "magic" numbers.  Fixes      *
* the snow seen when accessing the framebuffer without it.                   *
* It just works (I hope).                                                    *
*                                                                            *
\****************************************************************************/

#define DEFAULT_GR_LWM 100
#define DEFAULT_VID_LWM 100
#define DEFAULT_GR_BURST_SIZE 256
#define DEFAULT_VID_BURST_SIZE 128
#define VIDEO		0
#define GRAPHICS	1
#define MPORT		2
#define ENGINE		3
#define GFIFO_SIZE	320
#define GFIFO_SIZE_128	256
#define MFIFO_SIZE	120
#define VFIFO_SIZE	256
#define	ABS(a)	(a>0?a:-a)
typedef struct {
  int gdrain_rate;
  int vdrain_rate;
  int mdrain_rate;
  int gburst_size;
  int vburst_size;
  char vid_en;
  char gr_en;
  int wcmocc, wcgocc, wcvocc, wcvlwm, wcglwm;
  int by_gfacc;
  char vid_only_once;
  char gr_only_once;
  char first_vacc;
  char first_gacc;
  char first_macc;
  int vocc;
  int gocc;
  int mocc;
  char cur;
  char engine_en;
  char converged;
  int priority;
} nv3_arb_info;
typedef struct {
  int graphics_lwm;
  int video_lwm;
  int graphics_burst_size;
  int video_burst_size;
  int graphics_hi_priority;
  int media_hi_priority;
  int rtl_values;
  int valid;
} nv3_fifo_info;
typedef struct {
  char pix_bpp;
  char enable_video;
  char gr_during_vid;
  char enable_mp;
  int memory_width;
  int video_scale;
  int pclk_khz;
  int mclk_khz;
  int mem_page_miss;
  int mem_latency;
  char mem_aligned;
} nv3_sim_state;
typedef struct {
  int graphics_lwm;
  int video_lwm;
  int graphics_burst_size;
  int video_burst_size;
  int valid;
} nv4_fifo_info;
typedef struct {
  int pclk_khz;
  int mclk_khz;
  int nvclk_khz;
  char mem_page_miss;
  char mem_latency;
  int memory_width;
  char enable_video;
  char gr_during_vid;
  char pix_bpp;
  char mem_aligned;
  char enable_mp;
} nv4_sim_state;
static int nv3_iterate(nv3_fifo_info *res_info, nv3_sim_state * state, nv3_arb_info *ainfo)
{
    int iter = 0;
    int tmp, t;
    int vfsize, mfsize, gfsize;
    int mburst_size = 32;
    int mmisses, gmisses, vmisses;
    int misses;
    int vlwm, glwm, mlwm;
    int last, next, cur;
    int max_gfsize ;
    long ns;

    vlwm = 0;
    glwm = 0;
    mlwm = 0;
    vfsize = 0;
    gfsize = 0;
    cur = ainfo->cur;
    mmisses = 2;
    gmisses = 2;
    vmisses = 2;
    if (ainfo->gburst_size == 128) max_gfsize = GFIFO_SIZE_128;
    else  max_gfsize = GFIFO_SIZE;
    max_gfsize = GFIFO_SIZE;
    while (1)
    {
        if (ainfo->vid_en)
        {
            if (ainfo->wcvocc > ainfo->vocc) ainfo->wcvocc = ainfo->vocc;
            if (ainfo->wcvlwm > vlwm) ainfo->wcvlwm = vlwm ;
            ns = 1000000 * ainfo->vburst_size/(state->memory_width/8)/state->mclk_khz;
            vfsize = ns * ainfo->vdrain_rate / 1000000;
            vfsize =  ainfo->wcvlwm - ainfo->vburst_size + vfsize;
        }
        if (state->enable_mp)
        {
            if (ainfo->wcmocc > ainfo->mocc) ainfo->wcmocc = ainfo->mocc;
        }
        if (ainfo->gr_en)
        {
            if (ainfo->wcglwm > glwm) ainfo->wcglwm = glwm ;
            if (ainfo->wcgocc > ainfo->gocc) ainfo->wcgocc = ainfo->gocc;
            ns = 1000000 * (ainfo->gburst_size/(state->memory_width/8))/state->mclk_khz;
            gfsize = ns *ainfo->gdrain_rate/1000000;
            gfsize = ainfo->wcglwm - ainfo->gburst_size + gfsize;
        }
        mfsize = 0;
        if (!state->gr_during_vid && ainfo->vid_en)
            if (ainfo->vid_en && (ainfo->vocc < 0) && !ainfo->vid_only_once)
                next = VIDEO;
            else if (ainfo->mocc < 0)
                next = MPORT;
            else if (ainfo->gocc< ainfo->by_gfacc)
                next = GRAPHICS;
            else return (0);
        else switch (ainfo->priority)
            {
                case VIDEO:
                    if (ainfo->vid_en && ainfo->vocc<0 && !ainfo->vid_only_once)
                        next = VIDEO;
                    else if (ainfo->gr_en && ainfo->gocc<0 && !ainfo->gr_only_once)
                        next = GRAPHICS;
                    else if (ainfo->mocc<0)
                        next = MPORT;
                    else    return (0);
                    break;
                case GRAPHICS:
                    if (ainfo->gr_en && ainfo->gocc<0 && !ainfo->gr_only_once)
                        next = GRAPHICS;
                    else if (ainfo->vid_en && ainfo->vocc<0 && !ainfo->vid_only_once)
                        next = VIDEO;
                    else if (ainfo->mocc<0)
                        next = MPORT;
                    else    return (0);
                    break;
                default:
                    if (ainfo->mocc<0)
                        next = MPORT;
                    else if (ainfo->gr_en && ainfo->gocc<0 && !ainfo->gr_only_once)
                        next = GRAPHICS;
                    else if (ainfo->vid_en && ainfo->vocc<0 && !ainfo->vid_only_once)
                        next = VIDEO;
                    else    return (0);
                    break;
            }
        last = cur;
        cur = next;
        iter++;
        switch (cur)
        {
            case VIDEO:
                if (last==cur)    misses = 0;
                else if (ainfo->first_vacc)   misses = vmisses;
                else    misses = 1;
                ainfo->first_vacc = 0;
                if (last!=cur)
                {
                    ns =  1000000 * (vmisses*state->mem_page_miss + state->mem_latency)/state->mclk_khz; 
                    vlwm = ns * ainfo->vdrain_rate/ 1000000;
                    vlwm = ainfo->vocc - vlwm;
                }
                ns = 1000000*(misses*state->mem_page_miss + ainfo->vburst_size)/(state->memory_width/8)/state->mclk_khz;
                ainfo->vocc = ainfo->vocc + ainfo->vburst_size - ns*ainfo->vdrain_rate/1000000;
                ainfo->gocc = ainfo->gocc - ns*ainfo->gdrain_rate/1000000;
                ainfo->mocc = ainfo->mocc - ns*ainfo->mdrain_rate/1000000;
                break;
            case GRAPHICS:
                if (last==cur)    misses = 0;
                else if (ainfo->first_gacc)   misses = gmisses;
                else    misses = 1;
                ainfo->first_gacc = 0;
                if (last!=cur)
                {
                    ns = 1000000*(gmisses*state->mem_page_miss + state->mem_latency)/state->mclk_khz ;
                    glwm = ns * ainfo->gdrain_rate/1000000;
                    glwm = ainfo->gocc - glwm;
                }
                ns = 1000000*(misses*state->mem_page_miss + ainfo->gburst_size/(state->memory_width/8))/state->mclk_khz;
                ainfo->vocc = ainfo->vocc + 0 - ns*ainfo->vdrain_rate/1000000;
                ainfo->gocc = ainfo->gocc + ainfo->gburst_size - ns*ainfo->gdrain_rate/1000000;
                ainfo->mocc = ainfo->mocc + 0 - ns*ainfo->mdrain_rate/1000000;
                break;
            default:
                if (last==cur)    misses = 0;
                else if (ainfo->first_macc)   misses = mmisses;
                else    misses = 1;
                ainfo->first_macc = 0;
                ns = 1000000*(misses*state->mem_page_miss + mburst_size/(state->memory_width/8))/state->mclk_khz;
                ainfo->vocc = ainfo->vocc + 0 - ns*ainfo->vdrain_rate/1000000;
                ainfo->gocc = ainfo->gocc + 0 - ns*ainfo->gdrain_rate/1000000;
                ainfo->mocc = ainfo->mocc + mburst_size - ns*ainfo->mdrain_rate/1000000;
                break;
        }
        if (iter>100)
        {
            ainfo->converged = 0;
            return (1);
        }
        ns = 1000000*ainfo->gburst_size/(state->memory_width/8)/state->mclk_khz;
        tmp = ns * ainfo->gdrain_rate/1000000;
        if (ABS(ainfo->gburst_size) + ((ABS(ainfo->wcglwm) + 16 ) & ~0x7)    - tmp > max_gfsize)
        {
            ainfo->converged = 0;
            return (1);
        }
        ns = 1000000*ainfo->vburst_size/(state->memory_width/8)/state->mclk_khz;
        tmp = ns * ainfo->vdrain_rate/1000000;
        if (ABS(ainfo->vburst_size) + (ABS(ainfo->wcvlwm + 32) & ~0xf)  - tmp> VFIFO_SIZE)
        {
            ainfo->converged = 0;
            return (1);
        }
        if (ABS(ainfo->gocc) > max_gfsize)
        {
            ainfo->converged = 0;
            return (1);
        }
        if (ABS(ainfo->vocc) > VFIFO_SIZE)
        {
            ainfo->converged = 0;
            return (1);
        }
        if (ABS(ainfo->mocc) > MFIFO_SIZE)
        {
            ainfo->converged = 0;
            return (1);
        }
        if (ABS(vfsize) > VFIFO_SIZE)
        {
            ainfo->converged = 0;
            return (1);
        }
        if (ABS(gfsize) > max_gfsize)
        {
            ainfo->converged = 0;
            return (1);
        }
        if (ABS(mfsize) > MFIFO_SIZE)
        {
            ainfo->converged = 0;
            return (1);
        }
    }
}
static char nv3_arb(nv3_fifo_info * res_info, nv3_sim_state * state,  nv3_arb_info *ainfo) 
{
    int  g, v, not_done;
    long ens, vns, mns, gns;
    int mmisses, gmisses, vmisses, eburst_size, mburst_size;
    int refresh_cycle;

    refresh_cycle = 0;
    refresh_cycle = 2*(state->mclk_khz/state->pclk_khz) + 5;
    mmisses = 2;
    if (state->mem_aligned) gmisses = 2;
    else    gmisses = 3;
    vmisses = 2;
    eburst_size = state->memory_width * 1;
    mburst_size = 32;
    gns = 1000000 * (gmisses*state->mem_page_miss + state->mem_latency)/state->mclk_khz;
    ainfo->by_gfacc = gns*ainfo->gdrain_rate/1000000;
    ainfo->wcmocc = 0;
    ainfo->wcgocc = 0;
    ainfo->wcvocc = 0;
    ainfo->wcvlwm = 0;
    ainfo->wcglwm = 0;
    ainfo->engine_en = 1;
    ainfo->converged = 1;
    if (ainfo->engine_en)
    {
        ens =  1000000*(state->mem_page_miss + eburst_size/(state->memory_width/8) +refresh_cycle)/state->mclk_khz;
        ainfo->mocc = state->enable_mp ? 0-ens*ainfo->mdrain_rate/1000000 : 0;
        ainfo->vocc = ainfo->vid_en ? 0-ens*ainfo->vdrain_rate/1000000 : 0;
        ainfo->gocc = ainfo->gr_en ? 0-ens*ainfo->gdrain_rate/1000000 : 0;
        ainfo->cur = ENGINE;
        ainfo->first_vacc = 1;
        ainfo->first_gacc = 1;
        ainfo->first_macc = 1;
        nv3_iterate(res_info, state,ainfo);
    }
    if (state->enable_mp)
    {
        mns = 1000000 * (mmisses*state->mem_page_miss + mburst_size/(state->memory_width/8) + refresh_cycle)/state->mclk_khz;
        ainfo->mocc = state->enable_mp ? 0 : mburst_size - mns*ainfo->mdrain_rate/1000000;
        ainfo->vocc = ainfo->vid_en ? 0 : 0- mns*ainfo->vdrain_rate/1000000;
        ainfo->gocc = ainfo->gr_en ? 0: 0- mns*ainfo->gdrain_rate/1000000;
        ainfo->cur = MPORT;
        ainfo->first_vacc = 1;
        ainfo->first_gacc = 1;
        ainfo->first_macc = 0;
        nv3_iterate(res_info, state,ainfo);
    }
    if (ainfo->gr_en)
    {
        ainfo->first_vacc = 1;
        ainfo->first_gacc = 0;
        ainfo->first_macc = 1;
        gns = 1000000*(gmisses*state->mem_page_miss + ainfo->gburst_size/(state->memory_width/8) + refresh_cycle)/state->mclk_khz;
        ainfo->gocc = ainfo->gburst_size - gns*ainfo->gdrain_rate/1000000;
        ainfo->vocc = ainfo->vid_en? 0-gns*ainfo->vdrain_rate/1000000 : 0;
        ainfo->mocc = state->enable_mp ?  0-gns*ainfo->mdrain_rate/1000000: 0;
        ainfo->cur = GRAPHICS;
        nv3_iterate(res_info, state,ainfo);
    }
    if (ainfo->vid_en)
    {
        ainfo->first_vacc = 0;
        ainfo->first_gacc = 1;
        ainfo->first_macc = 1;
        vns = 1000000*(vmisses*state->mem_page_miss + ainfo->vburst_size/(state->memory_width/8) + refresh_cycle)/state->mclk_khz;
        ainfo->vocc = ainfo->vburst_size - vns*ainfo->vdrain_rate/1000000;
        ainfo->gocc = ainfo->gr_en? (0-vns*ainfo->gdrain_rate/1000000) : 0;
        ainfo->mocc = state->enable_mp? 0-vns*ainfo->mdrain_rate/1000000 :0 ;
        ainfo->cur = VIDEO;
        nv3_iterate(res_info, state, ainfo);
    }
    if (ainfo->converged)
    {
        res_info->graphics_lwm = (int)ABS(ainfo->wcglwm) + 16;
        res_info->video_lwm = (int)ABS(ainfo->wcvlwm) + 32;
        res_info->graphics_burst_size = ainfo->gburst_size;
        res_info->video_burst_size = ainfo->vburst_size;
        res_info->graphics_hi_priority = (ainfo->priority == GRAPHICS);
        res_info->media_hi_priority = (ainfo->priority == MPORT);
        if (res_info->video_lwm > 160)
        {
            res_info->graphics_lwm = 256;
            res_info->video_lwm = 128;
            res_info->graphics_burst_size = 64;
            res_info->video_burst_size = 64;
            res_info->graphics_hi_priority = 0;
            res_info->media_hi_priority = 0;
            ainfo->converged = 0;
            return (0);
        }
        if (res_info->video_lwm > 128)
        {
            res_info->video_lwm = 128;
        }
        return (1);
    }
    else
    {
        res_info->graphics_lwm = 256;
        res_info->video_lwm = 128;
        res_info->graphics_burst_size = 64;
        res_info->video_burst_size = 64;
        res_info->graphics_hi_priority = 0;
        res_info->media_hi_priority = 0;
        return (0);
    }
}
static char nv3_get_param(nv3_fifo_info *res_info, nv3_sim_state * state, nv3_arb_info *ainfo)
{
    int done, g,v, p;
    int priority, gburst_size, vburst_size, iter;
    
    done = 0;
    if (state->gr_during_vid && ainfo->vid_en)
        ainfo->priority = MPORT;
    else
        ainfo->priority = ainfo->gdrain_rate < ainfo->vdrain_rate ? VIDEO: GRAPHICS;
    for (p=0; p < 2 && done != 1; p++)
    {
        for (g=128 ; (g > 32) && (done != 1); g= g>> 1)
        {
            for (v=128; (v >=32) && (done !=1); v = v>> 1)
            {
                ainfo->priority = p;
                ainfo->gburst_size = g;     
                ainfo->vburst_size = v;
                done = nv3_arb(res_info, state,ainfo);
                if (g==128)
                {
                    if ((res_info->graphics_lwm + g) > 256)
                        done = 0;
                }
            }
        }
    }
    if (!done)
        return (0);
    else
        return (1);
}
static void nv3CalcArbitration 
(
    nv3_fifo_info * res_info,
    nv3_sim_state * state
)
{
    nv3_fifo_info save_info;
    nv3_arb_info ainfo;
    char   res_gr, res_vid;

    ainfo.gr_en = 1;
    ainfo.vid_en = state->enable_video;
    ainfo.vid_only_once = 0;
    ainfo.gr_only_once = 0;
    ainfo.gdrain_rate = (int) state->pclk_khz * state -> pix_bpp/8;
    ainfo.vdrain_rate = (int) state->pclk_khz * 2;
    if (state->video_scale != 0)
        ainfo.vdrain_rate = ainfo.vdrain_rate/state->video_scale;
    ainfo.mdrain_rate = 33000;
    res_info->rtl_values = 0;
    if (!state->gr_during_vid && state->enable_video)
    {
        ainfo.gr_only_once = 1;
        ainfo.gr_en = 1;
        ainfo.gdrain_rate = 0;
        res_vid = nv3_get_param(res_info, state,  &ainfo);
        res_vid = ainfo.converged;
        save_info.video_lwm = res_info->video_lwm;
        save_info.video_burst_size = res_info->video_burst_size;
        ainfo.vid_en = 1;
        ainfo.vid_only_once = 1;
        ainfo.gr_en = 1;
        ainfo.gdrain_rate = (int) state->pclk_khz * state -> pix_bpp/8;
        ainfo.vdrain_rate = 0;
        res_gr = nv3_get_param(res_info, state,  &ainfo);
        res_gr = ainfo.converged;
        res_info->video_lwm = save_info.video_lwm;
        res_info->video_burst_size = save_info.video_burst_size;
        res_info->valid = res_gr & res_vid;
    }
    else
    {
        if (!ainfo.gr_en) ainfo.gdrain_rate = 0;
        if (!ainfo.vid_en) ainfo.vdrain_rate = 0;
        res_gr = nv3_get_param(res_info, state,  &ainfo);
        res_info->valid = ainfo.converged;
    }
}
void nv3UpdateArbitrationSettings
(
    unsigned      VClk, 
    unsigned      pixelDepth, 
    unsigned     *burst,
    unsigned     *lwm,
    RIVA_HW_INST *chip
)
{
    nv3_fifo_info fifo_data;
    nv3_sim_state sim_data;
    unsigned int M, N, P, pll, MClk;
    
    pll = chip->PRAMDAC[0x00000504/4];
    M = (pll >> 0) & 0xFF; N = (pll >> 8) & 0xFF; P = (pll >> 16) & 0x0F;
    MClk = (N * chip->CrystalFreqKHz / M) >> P;
    sim_data.pix_bpp        = (char)pixelDepth;
    sim_data.enable_video   = 0;
    sim_data.enable_mp      = 0;
    sim_data.video_scale    = 1;
    sim_data.memory_width   = (chip->PEXTDEV[0x00000000/4] & 0x10) ? 128 : 64;
    sim_data.memory_width   = 128;
    sim_data.mem_latency    = 11;
    sim_data.mem_aligned    = 1;
    sim_data.mem_page_miss  = 9;
    sim_data.gr_during_vid  = 0;
    sim_data.pclk_khz       = VClk;
    sim_data.mclk_khz       = MClk;
    nv3CalcArbitration(&fifo_data, &sim_data);
    if (fifo_data.valid)
    {
        int  b = fifo_data.graphics_burst_size >> 4;
        *burst = 0;
        while (b >>= 1) (*burst)++;
        *lwm   = fifo_data.graphics_lwm >> 3;
    }
    else
    {
        *lwm   = 0x24;
        *burst = 0x02;
    }
}
static void nv4CalcArbitration 
(
    nv4_fifo_info *fifo,
    nv4_sim_state *arb
)
{
    int data, m,n,p, pagemiss, cas,width, video_enable, color_key_enable, bpp, align;
    int nvclks, mclks, pclks, vpagemiss, crtpagemiss, vbs;
    int found, mclk_extra, mclk_loop, cbs, m1, p1;
    int xtal_freq, mclk_freq, pclk_freq, nvclk_freq, mp_enable;
    int us_m, us_n, us_p, video_drain_rate, crtc_drain_rate;
    int vpm_us, us_video, vlwm, video_fill_us, cpm_us, us_crt,clwm;
    int craw, vraw;

    fifo->valid = 1;
    pclk_freq = arb->pclk_khz;
    mclk_freq = arb->mclk_khz;
    nvclk_freq = arb->nvclk_khz;
    pagemiss = arb->mem_page_miss;
    cas = arb->mem_latency;
    width = arb->memory_width >> 6;
    video_enable = arb->enable_video;
    color_key_enable = arb->gr_during_vid;
    bpp = arb->pix_bpp;
    align = arb->mem_aligned;
    mp_enable = arb->enable_mp;
    clwm = 0;
    vlwm = 0;
    cbs = 128;
    pclks = 2;
    nvclks = 2;
    nvclks += 2;
    nvclks += 1;
    mclks = 5;
    mclks += 3;
    mclks += 1;
    mclks += cas;
    mclks += 1;
    mclks += 1;
    mclks += 1;
    mclks += 1;
    mclk_extra = 3;
    nvclks += 2;
    nvclks += 1;
    nvclks += 1;
    nvclks += 1;
    if (mp_enable)
        mclks+=4;
    nvclks += 0;
    pclks += 0;
    found = 0;
    while (found != 1)
    {
        fifo->valid = 1;
        found = 1;
        mclk_loop = mclks+mclk_extra;
        us_m = mclk_loop *1000*1000 / mclk_freq;
        us_n = nvclks*1000*1000 / nvclk_freq;
        us_p = nvclks*1000*1000 / pclk_freq;
        if (video_enable)
        {
            video_drain_rate = pclk_freq * 2;
            crtc_drain_rate = pclk_freq * bpp/8;
            vpagemiss = 2;
            vpagemiss += 1;
            crtpagemiss = 2;
            vpm_us = (vpagemiss * pagemiss)*1000*1000/mclk_freq;
            if (nvclk_freq * 2 > mclk_freq * width)
                video_fill_us = cbs*1000*1000 / 16 / nvclk_freq ;
            else
                video_fill_us = cbs*1000*1000 / (8 * width) / mclk_freq;
            us_video = vpm_us + us_m + us_n + us_p + video_fill_us;
            vlwm = us_video * video_drain_rate/(1000*1000);
            vlwm++;
            vbs = 128;
            if (vlwm > 128) vbs = 64;
            if (vlwm > (256-64)) vbs = 32;
            if (nvclk_freq * 2 > mclk_freq * width)
                video_fill_us = vbs *1000*1000/ 16 / nvclk_freq ;
            else
                video_fill_us = vbs*1000*1000 / (8 * width) / mclk_freq;
            cpm_us = crtpagemiss  * pagemiss *1000*1000/ mclk_freq;
            us_crt =
            us_video
            +video_fill_us
            +cpm_us
            +us_m + us_n +us_p
            ;
            clwm = us_crt * crtc_drain_rate/(1000*1000);
            clwm++;
        }
        else
        {
            crtc_drain_rate = pclk_freq * bpp/8;
            crtpagemiss = 2;
            crtpagemiss += 1;
            cpm_us = crtpagemiss  * pagemiss *1000*1000/ mclk_freq;
            us_crt =  cpm_us + us_m + us_n + us_p ;
            clwm = us_crt * crtc_drain_rate/(1000*1000);
            clwm++;
        }
        m1 = clwm + cbs - 512;
        p1 = m1 * pclk_freq / mclk_freq;
        p1 = p1 * bpp / 8;
        if ((p1 < m1) && (m1 > 0))
        {
            fifo->valid = 0;
            found = 0;
            if (mclk_extra ==0)   found = 1;
            mclk_extra--;
        }
        else if (video_enable)
        {
            if ((clwm > 511) || (vlwm > 255))
            {
                fifo->valid = 0;
                found = 0;
                if (mclk_extra ==0)   found = 1;
                mclk_extra--;
            }
        }
        else
        {
            if (clwm > 519)
            {
                fifo->valid = 0;
                found = 0;
                if (mclk_extra ==0)   found = 1;
                mclk_extra--;
            }
        }
        craw = clwm;
        vraw = vlwm;
        if (clwm < 384) clwm = 384;
        if (vlwm < 128) vlwm = 128;
        data = (int)(clwm);
        fifo->graphics_lwm = data;
        fifo->graphics_burst_size = 128;
        data = (int)((vlwm+15));
        fifo->video_lwm = data;
        fifo->video_burst_size = vbs;
    }
}
static void nv4UpdateArbitrationSettings
(
    unsigned      VClk, 
    unsigned      pixelDepth, 
    unsigned     *burst,
    unsigned     *lwm,
    RIVA_HW_INST *chip
)
{
    nv4_fifo_info fifo_data;
    nv4_sim_state sim_data;
    unsigned int M, N, P, pll, MClk, NVClk, cfg1;

    pll = chip->PRAMDAC[0x00000504/4];
    M = (pll >> 0)  & 0xFF; N = (pll >> 8)  & 0xFF; P = (pll >> 16) & 0x0F;
    MClk  = (N * chip->CrystalFreqKHz / M) >> P;
    pll = chip->PRAMDAC[0x00000500/4];
    M = (pll >> 0)  & 0xFF; N = (pll >> 8)  & 0xFF; P = (pll >> 16) & 0x0F;
    NVClk  = (N * chip->CrystalFreqKHz / M) >> P;
    cfg1 = chip->PFB[0x00000204/4];
    sim_data.pix_bpp        = (char)pixelDepth;
    sim_data.enable_video   = 0;
    sim_data.enable_mp      = 0;
    sim_data.memory_width   = (chip->PEXTDEV[0x00000000/4] & 0x10) ? 128 : 64;
    sim_data.mem_latency    = (char)cfg1 & 0x0F;
    sim_data.mem_aligned    = 1;
    sim_data.mem_page_miss  = (char)(((cfg1 >> 4) &0x0F) + ((cfg1 >> 31) & 0x01));
    sim_data.gr_during_vid  = 0;
    sim_data.pclk_khz       = VClk;
    sim_data.mclk_khz       = MClk;
    sim_data.nvclk_khz      = NVClk;
    nv4CalcArbitration(&fifo_data, &sim_data);
    if (fifo_data.valid)
    {
        int  b = fifo_data.graphics_burst_size >> 4;
        *burst = 0;
        while (b >>= 1) (*burst)++;
        *lwm   = fifo_data.graphics_lwm >> 3;
    }
}

/****************************************************************************\
*                                                                            *
*                          RIVA Mode State Routines                          *
*                                                                            *
\****************************************************************************/

/*
 * Calculate the Video Clock parameters for the PLL.
 */
static int CalcVClock
(
    int           clockIn,
    int          *clockOut,
    int          *mOut,
    int          *nOut,
    int          *pOut,
    RIVA_HW_INST *chip
)
{
    unsigned lowM, highM, highP;
    unsigned DeltaNew, DeltaOld;
    unsigned VClk, Freq;
    unsigned M, N, O, P;
    
    DeltaOld = 0xFFFFFFFF;
    VClk     = (unsigned)clockIn;
    if (chip->CrystalFreqKHz == 14318)
    {
        lowM  = 8;
        highM = 14 - (chip->Architecture == 3);
    }
    else
    {
        lowM  = 7;
        highM = 13 - (chip->Architecture == 3);
    }                      
    highP = 4 - (chip->Architecture == 3);
    for (P = 0; P <= highP; P ++)
    {
        Freq = VClk << P;
        if ((Freq >= 128000) && (Freq <= chip->MaxVClockFreqKHz))
        {
            for (M = lowM; M <= highM; M++)
            {
                N    = (VClk * M / chip->CrystalFreqKHz) << P;
                Freq = (chip->CrystalFreqKHz * N / M) >> P;
                if (Freq > VClk)
                    DeltaNew = Freq - VClk;
                else
                    DeltaNew = VClk - Freq;
                if (DeltaNew < DeltaOld)
                {
                    *mOut     = M;
                    *nOut     = N;
                    *pOut     = P;
                    *clockOut = Freq;
                    DeltaOld  = DeltaNew;
                }
            }
        }
    }
    return (DeltaOld != 0xFFFFFFFF);
}
/*
 * Calculate extended mode parameters (SVGA) and save in a 
 * mode state structure.
 */
static void CalcStateExt
(
    RIVA_HW_INST  *chip,
    RIVA_HW_STATE *state,
    int            bpp,
    int            width,
    int            hDisplaySize,
    int            hDisplay,
    int            hStart,
    int            hEnd,
    int            hTotal,
    int            height,
    int            vDisplay,
    int            vStart,
    int            vEnd,
    int            vTotal,
    int            dotClock
)
{
    int pixelDepth, VClk, m, n, p;
    /*
     * Save mode parameters.
     */
    state->bpp    = bpp;
    state->width  = width;
    state->height = height;
    /*
     * Extended RIVA registers.
     */
    pixelDepth = (bpp + 1)/8;
    CalcVClock(dotClock, &VClk, &m, &n, &p, chip);
    switch (chip->Architecture)
    {
	    case 3:
            nv3UpdateArbitrationSettings(VClk, 
                                         pixelDepth * 8, 
                                        &(state->arbitration0),
                                        &(state->arbitration1),
                                         chip);
            state->cursor0  = 0x00;
            state->cursor1  = 0x78;
            state->cursor2  = 0x00000000;
            state->pllsel   = 0x10010100;
            state->config   = ((width + 31)/32)
                            | (((pixelDepth > 2) ? 3 : pixelDepth) << 8)
                            | 0x1000;
            state->general  = 0x00000100;
            state->repaint1 = hDisplaySize < 1280 ? 0x06 : 0x02;
            break;
	    case 4:
	    case 5:
            nv4UpdateArbitrationSettings(VClk, 
                                         pixelDepth * 8, 
                                        &(state->arbitration0),
                                        &(state->arbitration1),
                                         chip);
            state->cursor0  = 0x00;
            state->cursor1  = 0xFC;
            state->cursor2  = 0x00000000;
            state->pllsel   = 0x10000700;
            state->config   = 0x00001114;
            state->general  = bpp == 16 ? 0x00101100 : 0x00100100;
            state->repaint1 = hDisplaySize < 1280 ? 0x04 : 0x00;
            break;
    }
    state->vpll     = (p << 16) | (n << 8) | m;
    state->screen   = ((hTotal   & 0x040) >> 2)
                    | ((vDisplay & 0x400) >> 7)
                    | ((vStart   & 0x400) >> 8)
                    | ((vDisplay & 0x400) >> 9)
                    | ((vTotal   & 0x400) >> 10);
    state->repaint0 = (((width/8)*pixelDepth) & 0x700) >> 3;
    state->horiz    = hTotal     < 260 ? 0x00 : 0x01;
    state->pixel    = (pixelDepth > 2 ? 3 : pixelDepth) | 0x40;
    state->offset0  =
    state->offset1  =
    state->offset2  =
    state->offset3  = 0;
    state->pitch0   =
    state->pitch1   =
    state->pitch2   =
    state->pitch3   = pixelDepth * width;
}
/*
 * Load fixed function state and pre-calculated/stored state.
 */
#define LOAD_FIXED_STATE(tbl,dev)                                       \
    for (i = 0; i < sizeof(tbl##Table##dev)/8; i++)                 \
        chip->dev[tbl##Table##dev[i][0]] = tbl##Table##dev[i][1]
#define LOAD_FIXED_STATE_8BPP(tbl,dev)                                  \
    for (i = 0; i < sizeof(tbl##Table##dev##_8BPP)/8; i++)            \
        chip->dev[tbl##Table##dev##_8BPP[i][0]] = tbl##Table##dev##_8BPP[i][1]
#define LOAD_FIXED_STATE_15BPP(tbl,dev)                                 \
    for (i = 0; i < sizeof(tbl##Table##dev##_15BPP)/8; i++)           \
        chip->dev[tbl##Table##dev##_15BPP[i][0]] = tbl##Table##dev##_15BPP[i][1]
#define LOAD_FIXED_STATE_16BPP(tbl,dev)                                 \
    for (i = 0; i < sizeof(tbl##Table##dev##_16BPP)/8; i++)           \
        chip->dev[tbl##Table##dev##_16BPP[i][0]] = tbl##Table##dev##_16BPP[i][1]
#define LOAD_FIXED_STATE_32BPP(tbl,dev)                                 \
    for (i = 0; i < sizeof(tbl##Table##dev##_32BPP)/8; i++)           \
        chip->dev[tbl##Table##dev##_32BPP[i][0]] = tbl##Table##dev##_32BPP[i][1]
static void LoadStateExt
(
    RIVA_HW_INST  *chip,
    RIVA_HW_STATE *state
)
{
    int i;
    /*
     * Load HW fixed function state.
     */
    LOAD_FIXED_STATE(Riva,PMC);
    LOAD_FIXED_STATE(Riva,PTIMER);
    /*
     * Make sure frame buffer config gets set before loading PRAMIN.
     */
    chip->PFB[0x00000200/4] = state->config;
    switch (chip->Architecture)
    {
        case 3:
            LOAD_FIXED_STATE(nv3,PFIFO);
            LOAD_FIXED_STATE(nv3,PRAMIN);
            LOAD_FIXED_STATE(nv3,PGRAPH);
            switch (state->bpp)
            {
                case 15:
                case 16:
                    LOAD_FIXED_STATE_15BPP(nv3,PRAMIN);
                    LOAD_FIXED_STATE_15BPP(nv3,PGRAPH);
                    chip->Tri03 = (RivaTexturedTriangle03  *)&(chip->FIFO[0x0000E000/4]);
                    break;
                case 24:
                case 32:
                    LOAD_FIXED_STATE_32BPP(nv3,PRAMIN);
                    LOAD_FIXED_STATE_32BPP(nv3,PGRAPH);
                    chip->Tri03 = 0L;
                    break;
                case 8:
                default:
                    LOAD_FIXED_STATE_8BPP(nv3,PRAMIN);
                    LOAD_FIXED_STATE_8BPP(nv3,PGRAPH);
                    chip->Tri03 = 0L;
                    break;
            }
            for (i = 0x00000; i < 0x00800; i++)
                chip->PRAMIN[0x00000502 + i] = (i << 12) | 0x03;
            chip->PGRAPH[0x00000630/4] = state->offset0;
            chip->PGRAPH[0x00000634/4] = state->offset1;
            chip->PGRAPH[0x00000638/4] = state->offset2;
            chip->PGRAPH[0x0000063C/4] = state->offset3;
            chip->PGRAPH[0x00000650/4] = state->pitch0;
            chip->PGRAPH[0x00000654/4] = state->pitch1;
            chip->PGRAPH[0x00000658/4] = state->pitch2;
            chip->PGRAPH[0x0000065C/4] = state->pitch3;
            break;
	    case 4:
	    case 5:
            LOAD_FIXED_STATE(nv4,PFIFO);
            LOAD_FIXED_STATE(nv4,PRAMIN);
            LOAD_FIXED_STATE(nv4,PGRAPH);
            switch (state->bpp)
            {
                case 15:
                    LOAD_FIXED_STATE_15BPP(nv4,PRAMIN);
                    LOAD_FIXED_STATE_15BPP(nv4,PGRAPH);
                    chip->Tri03 = (RivaTexturedTriangle03  *)&(chip->FIFO[0x0000E000/4]);
                    break;
                case 16:
                    LOAD_FIXED_STATE_16BPP(nv4,PRAMIN);
                    LOAD_FIXED_STATE_16BPP(nv4,PGRAPH);
                    chip->Tri03 = (RivaTexturedTriangle03  *)&(chip->FIFO[0x0000E000/4]);
                    break;
                case 24:
                case 32:
                    LOAD_FIXED_STATE_32BPP(nv4,PRAMIN);
                    LOAD_FIXED_STATE_32BPP(nv4,PGRAPH);
                    chip->Tri03 = 0L;
                    break;
                case 8:
                default:
                    LOAD_FIXED_STATE_8BPP(nv4,PRAMIN);
                    LOAD_FIXED_STATE_8BPP(nv4,PGRAPH);
                    chip->Tri03 = 0L;
                    break;
            }
            chip->PGRAPH[0x00000640/4] = state->offset0;
            chip->PGRAPH[0x00000644/4] = state->offset1;
            chip->PGRAPH[0x00000648/4] = state->offset2;
            chip->PGRAPH[0x0000064C/4] = state->offset3;
            chip->PGRAPH[0x00000670/4] = state->pitch0;
            chip->PGRAPH[0x00000674/4] = state->pitch1;
            chip->PGRAPH[0x00000678/4] = state->pitch2;
            chip->PGRAPH[0x0000067C/4] = state->pitch3;
            break;
    }
//NOTICE("8");
//    LOAD_FIXED_STATE(Riva,FIFO); /* FIX ME*/
//NOTICE("9");
    /*
     * Load HW mode state.
     */
    outb(0x19, 0x3D4); outb(state->repaint0, 0x3D5);
    outb(0x1A, 0x3D4); outb(state->repaint1, 0x3D5);
    outb(0x25, 0x3D4); outb(state->screen, 0x3D5);
    outb(0x28, 0x3D4); outb(state->pixel, 0x3D5);
    outb(0x2D, 0x3D4); outb(state->horiz, 0x3D5);
    outb(0x1B, 0x3D4); outb(state->arbitration0, 0x3D5);
    outb(0x20, 0x3D4); outb(state->arbitration1, 0x3D5);
    outb(0x30, 0x3D4); outb(state->cursor0, 0x3D5);
    outb(0x31, 0x3D4); outb(state->cursor1, 0x3D5);
    chip->PRAMDAC[0x00000300/4]  = state->cursor2;
    chip->PRAMDAC[0x00000508/4]  = state->vpll;
    chip->PRAMDAC[0x0000050C/4]  = state->pllsel;
    chip->PRAMDAC[0x00000600/4]  = state->general;
    /*
     * Turn off VBlank enable and reset.
     */
//    *(chip->VBLANKENABLE) = 0;		/* FIXME*/
//    *(chip->VBLANK)       = chip->VBlankBit; /*FIXME*/
    /*
     * Set interrupt enable.
     */    
    chip->PMC[0x00000140/4]  = chip->EnableIRQ & 0x01;
    /*
     * Set current state pointer.
     */
    chip->CurrentState = state;
    /*
     * Reset FIFO free count.
     */
    chip->FifoFreeCount = 0;
}
static void UnloadStateExt
(
    RIVA_HW_INST  *chip,
    RIVA_HW_STATE *state
)
{
    /*
     * Save current HW state.
     */
    outb(0x19, 0x3D4); state->repaint0     = inb(0x3D5);
    outb(0x1A, 0x3D4); state->repaint1     = inb(0x3D5);
    outb(0x25, 0x3D4); state->screen       = inb(0x3D5);
    outb(0x28, 0x3D4); state->pixel        = inb(0x3D5);
    outb(0x2D, 0x3D4); state->horiz        = inb(0x3D5);
    outb(0x1B, 0x3D4); state->arbitration0 = inb(0x3D5);
    outb(0x20, 0x3D4); state->arbitration1 = inb(0x3D5);
    outb(0x30, 0x3D4); state->cursor0      = inb(0x3D5);
    outb(0x31, 0x3D4); state->cursor1      = inb(0x3D5);
                      state->cursor2      = chip->PRAMDAC[0x00000300/4];
                      state->vpll         = chip->PRAMDAC[0x00000508/4];
                      state->pllsel       = chip->PRAMDAC[0x0000050C/4];
                      state->general      = chip->PRAMDAC[0x00000600/4];
                      state->config       = chip->PFB[0x00000200/4];
                      switch (chip->Architecture)
                      {
			      case 3:
                              state->offset0  = chip->PGRAPH[0x00000630/4];
                              state->offset1  = chip->PGRAPH[0x00000634/4];
                              state->offset2  = chip->PGRAPH[0x00000638/4];
                              state->offset3  = chip->PGRAPH[0x0000063C/4];
                              state->pitch0   = chip->PGRAPH[0x00000650/4];
                              state->pitch1   = chip->PGRAPH[0x00000654/4];
                              state->pitch2   = chip->PGRAPH[0x00000658/4];
                              state->pitch3   = chip->PGRAPH[0x0000065C/4];
                              break;
			      case 4:
			      case 5:
                              state->offset0  = chip->PGRAPH[0x00000640/4];
                              state->offset1  = chip->PGRAPH[0x00000644/4];
                              state->offset2  = chip->PGRAPH[0x00000648/4];
                              state->offset3  = chip->PGRAPH[0x0000064C/4];
                              state->pitch0   = chip->PGRAPH[0x00000670/4];
                              state->pitch1   = chip->PGRAPH[0x00000674/4];
                              state->pitch2   = chip->PGRAPH[0x00000678/4];
                              state->pitch3   = chip->PGRAPH[0x0000067C/4];
                              break;
                      }
}
static void SetStartAddress
(
    RIVA_HW_INST *chip,
    unsigned      start
)
{
    int offset = start >> 2;
    int pan    = (start & 3) << 1;
    unsigned char tmp;

    /*
     * Unlock extended registers.
     */
    outb(chip->LockUnlockIndex, chip->LockUnlockIO);
    outb(0x57, chip->LockUnlockIO + 1);
    /*
     * Set start address.
     */
    outb(0x0D, 0x3D4);
    outb(offset, 0x3D5);
    outb(0x0C, 0x3D4);
    outb(offset >> 8, 0x3D5);
    outb(0x19, 0x3D4);
    tmp = inb(0x3D5);
    outb(((offset >> 16) & 0x0F) | (tmp & 0xF0), 0x3D5);
    /*
     * 4 pixel pan register.
     */
    offset = inb(chip->IO + 0x0A);
    outb(0x13, 0x3C0);
    outb(pan, 0x3C0);
}
static void nv3SetSurfaces2D
(
    RIVA_HW_INST *chip,
    unsigned     surf0,
    unsigned     surf1
)
{
    while (nv3Busy(chip));
    chip->PGRAPH[0x00000630/4] = surf0;
    chip->PGRAPH[0x00000634/4] = surf1;
}
static void nv4SetSurfaces2D
(
    RIVA_HW_INST *chip,
    unsigned     surf0,
    unsigned     surf1
)
{
    while (nv4Busy(chip));
    chip->PGRAPH[0x00000640/4] = surf0;
    chip->PGRAPH[0x00000644/4] = surf1;
}
static void nv3SetSurfaces3D
(
    RIVA_HW_INST *chip,
    unsigned     surf0,
    unsigned     surf1
)
{
    while (nv3Busy(chip));
    chip->PGRAPH[0x00000638/4] = surf0;
    chip->PGRAPH[0x0000063C/4] = surf1;
}
static void nv4SetSurfaces3D
(
    RIVA_HW_INST *chip,
    unsigned     surf0,
    unsigned     surf1
)
{
    while (nv4Busy(chip));
    chip->PGRAPH[0x00000648/4] = surf0;
    chip->PGRAPH[0x0000064C/4] = surf1;
}

/****************************************************************************\
*                                                                            *
*                      Probe RIVA Chip Configuration                         *
*                                                                            *
\****************************************************************************/

void nv3GetConfig
(
    RIVA_HW_INST *chip
)
{
    /*
     * Fill in chip configuration.
     */
    if (chip->PFB[0x00000000/4] & 0x00000020)
    {
        if (((chip->PMC[0x00000000/4] & 0xF0) == 0x20)
         && ((chip->PMC[0x00000000/4] & 0x0F) >= 0x02))
        {        
            /*
             * SDRAM 128 ZX.
             */
            chip->RamBandwidthKBytesPerSec = 800000;
            switch (chip->PFB[0x00000000/4] & 0x03)
            {
                case 2:
                    chip->RamAmountKBytes = 1024 * 4 - 32;
                    break;
                case 1:
                    chip->RamAmountKBytes = 1024 * 2 - 32;
                    break;
                default:
                    chip->RamAmountKBytes = 1024 * 8 - 32;
                    break;
            }
        }            
        else            
        {
            chip->RamBandwidthKBytesPerSec = 1000000;
            chip->RamAmountKBytes          = 1024 * 8 - 32;
        }            
    }
    else
    {
        /*
         * SGRAM 128.
         */
        chip->RamBandwidthKBytesPerSec = 1000000;
        switch (chip->PFB[0x00000000/4] & 0x00000003)
        {
            case 0:
                chip->RamAmountKBytes = 1024 * 8 - 32;
                break;
            case 2:
                chip->RamAmountKBytes = 1024 * 4 - 32;
                break;
            default:
                chip->RamAmountKBytes = 1024 * 2 - 32;
                break;
        }
    }        
    chip->CrystalFreqKHz   = (chip->PEXTDEV[0x00000000/4] & 0x00000020) ? 14318 : 13500;
    chip->CURSOR           = &(chip->PRAMIN[0x00008000/4 - 0x0800/4]);
    chip->CURSORPOS        = &(chip->PRAMDAC[0x0300/4]);
    chip->VBLANKENABLE     = &(chip->PGRAPH[0x0140/4]);
    chip->VBLANK           = &(chip->PGRAPH[0x0100/4]);
    chip->VBlankBit        = 0x00000100;
    chip->MaxVClockFreqKHz = 230000;
    chip->LockUnlockIO     = 0x3C4;
    chip->LockUnlockIndex  = 0x06;
    /*
     * Set chip functions.
     */
    chip->Busy            = nv3Busy;
    chip->ShowHideCursor  = ShowHideCursor;
    chip->CalcStateExt    = CalcStateExt;
    chip->LoadStateExt    = LoadStateExt;
    chip->UnloadStateExt  = UnloadStateExt;
    chip->SetStartAddress = SetStartAddress;
    chip->SetSurfaces2D   = nv3SetSurfaces2D;
    chip->SetSurfaces3D   = nv3SetSurfaces3D;
}

void nv4GetConfig
(
    RIVA_HW_INST *chip
)
{
    /*
     * Fill in chip configuration.
     */
    switch (chip->PFB[0x00000000/4] & 0x00000003)
    {
        case 0:
            chip->RamAmountKBytes = 1024 * 32 - 128;
            break;
        case 1:
            chip->RamAmountKBytes = 1024 * 4 - 128;
            break;
        case 2:
            chip->RamAmountKBytes = 1024 * 8 - 128;
            break;
        case 3:
        default:
            chip->RamAmountKBytes = 1024 * 16 - 128;
            break;
    }
    switch ((chip->PFB[0x00000000/4] >> 3) & 0x00000003)
    {
        case 3:
            chip->RamBandwidthKBytesPerSec = 800000;
            break;
        default:
            chip->RamBandwidthKBytesPerSec = 1000000;
            break;
    }
    chip->CrystalFreqKHz   = (chip->PEXTDEV[0x00000000/4] & 0x00000040) ? 14318 : 13500;
    chip->CURSOR           = &(chip->PRAMIN[0x00010000/4 - 0x0800/4]);
    chip->CURSORPOS        = &(chip->PRAMDAC[0x0300/4]);
    chip->VBLANKENABLE     = &(chip->PCRTC[0x0140/4]);
    chip->VBLANK           = &(chip->PCRTC[0x0100/4]);
    chip->VBlankBit        = 0x00000001;
    chip->MaxVClockFreqKHz = 250000;
    chip->LockUnlockIO     = 0x3D4;
    chip->LockUnlockIndex  = 0x1F;
    /*
     * Set chip functions.
     */
    chip->Busy            = nv4Busy;
    chip->ShowHideCursor  = ShowHideCursor;
    chip->CalcStateExt    = CalcStateExt;
    chip->LoadStateExt    = LoadStateExt;
    chip->UnloadStateExt  = UnloadStateExt;
    chip->SetStartAddress = SetStartAddress;
    chip->SetSurfaces2D   = nv4SetSurfaces2D;
    chip->SetSurfaces3D   = nv4SetSurfaces3D;
}

void nv5GetConfig
(
    RIVA_HW_INST *chip
)
{
    /*
     * Fill in chip configuration.
     */
    switch (chip->PFB[0x00000000/4] & 0x00000003)
    {
        case 0:
            chip->RamAmountKBytes = 1024 * 32 - 128;
            break;
        case 1:
            chip->RamAmountKBytes = 1024 * 4 - 128;
            break;
        case 2:
            chip->RamAmountKBytes = 1024 * 8 - 128;
            break;
        case 3:
        default:
            chip->RamAmountKBytes = 1024 * 16 - 128;
            break;
    }
    switch ((chip->PFB[0x00000000/4] >> 3) & 0x00000003)
    {
        case 3:
            chip->RamBandwidthKBytesPerSec = 800000;
            break;
        default:
            chip->RamBandwidthKBytesPerSec = 1000000;
            break;
    }
    chip->CrystalFreqKHz   = (chip->PEXTDEV[0x00000000/4] & 0x00000040) ? 14318 : 13500;
    chip->CURSOR           = &(chip->PRAMIN[0x00010000/4 - 0x0800/4]);
    chip->CURSORPOS        = &(chip->PRAMDAC[0x0300/4]);
    chip->VBLANKENABLE     = &(chip->PCRTC[0x0140/4]);
    chip->VBLANK           = &(chip->PCRTC[0x0100/4]);
    chip->VBlankBit        = 0x00000001;
    chip->MaxVClockFreqKHz = 250000;
    chip->LockUnlockIO     = 0x3D4;
    chip->LockUnlockIndex  = 0x1F;
    /*
     * Set chip functions.
     */
    chip->Busy            = nv4Busy;
    chip->ShowHideCursor  = ShowHideCursor;
    chip->CalcStateExt    = CalcStateExt;
    chip->LoadStateExt    = LoadStateExt;
    chip->UnloadStateExt  = UnloadStateExt;
    chip->SetStartAddress = SetStartAddress;
    chip->SetSurfaces2D   = nv4SetSurfaces2D;
    chip->SetSurfaces3D   = nv4SetSurfaces3D;
}

int RivaGetConfig
(
    RIVA_HW_INST *chip
)
{
    /*
     * Save this so future SW know whats it's dealing with.
     */
    chip->Version = RIVA_SW_VERSION;
    /*
     * Chip specific configuration.
     */
    switch (chip->Architecture)
    {
        case 3:
            nv3GetConfig(chip);
            break;
        case 4:
            nv4GetConfig(chip);
            break;
	    case 5:
	    nv5GetConfig(chip);
        default:
            return (-1);
    }
    /*
     * Fill in FIFO pointers.
     */
    chip->Rop    = (RivaRop                 *)&(chip->FIFO[0x00000000/4]);
    chip->Clip   = (RivaClip                *)&(chip->FIFO[0x00002000/4]);
    chip->Patt   = (RivaPattern             *)&(chip->FIFO[0x00004000/4]);
    chip->Pixmap = (RivaPixmap              *)&(chip->FIFO[0x00006000/4]);
    chip->Blt    = (RivaScreenBlt           *)&(chip->FIFO[0x00008000/4]);
    chip->Bitmap = (RivaBitmap              *)&(chip->FIFO[0x0000A000/4]);
    chip->Tri03  = (RivaTexturedTriangle03  *)&(chip->FIFO[0x0000E000/4]);
    return (0);
}

