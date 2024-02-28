/*
* Copyright (c) 2015-2024 Cadence Design Systems Inc.
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
/*******************************************************************************
 * xa-mimo-mix4.c
 *
 * Sample mimo_mix4 plugin
 ******************************************************************************/

#define MODULE_TAG                      MIMO_MIX4

/*******************************************************************************
 * Includes
 ******************************************************************************/

#include <stdint.h>
#include <string.h>

#include "audio/xa-mimo-mix-api.h"

/* ...debugging facility */
#include "xf-debug.h"

#ifdef XAF_PROFILE
#include "xaf-clk-test.h"
extern clk_t mimo_mix_cycles;
#endif


#define DSP_SATURATE_S16(WORD32)   \
    (WORD16)((WORD32) > 0x7fff ? 0x7fff : ((WORD32) < -0x8000 ? -0x8000 : (WORD32)))

/*******************************************************************************
 * Internal plugin-specific definitions
 ******************************************************************************/

#define XA_MIMO_CFG_DEFAULT_PCM_WIDTH	16
#define XA_MIMO_CFG_DEFAULT_CHANNELS	2
#define XA_MIMO_CFG_DEFAULT_FRAMESIZE	512

#define XA_MIMO_CFG_SCRATCH_SIZE	1024

#define XA_MIMO_IN_PORTS		4
#define XA_MIMO_OUT_PORTS		1

#define _MAX(a, b)	(((a) > (b))?(a):(b))
#define _MIN(a, b)	(((a) < (b))?(a):(b))

/*******************************************************************************
 * Aec state flags
 ******************************************************************************/

#define XA_AEC_FLAG_PREINIT_DONE      (1 << 0)
#define XA_AEC_FLAG_POSTINIT_DONE     (1 << 1)
#define XA_AEC_FLAG_RUNNING           (1 << 2)
#define XA_AEC_FLAG_OUTPUT            (1 << 3)
#define XA_AEC_FLAG_INPUT_OVER        (1 << 4)
#define XA_AEC_FLAG_COMPLETE          (1 << 5)
#define XA_AEC_FLAG_EXEC_DONE         (1 << 6)
#define XA_AEC_FLAG_PORT_PAUSED       (1 << 7)
#define XA_AEC_FLAG_PORT_CONNECTED    (1 << 8)

/*******************************************************************************
 * Internal functions definitions
 ******************************************************************************/

/* ...API structure */
typedef struct XAPcmAec
{
    /* ...aec state */
    UWORD32                 state;

    /* ...number of samples in a frame */
    UWORD32                 frame_size;

    /* ...sampling rate */
    UWORD32                 sample_rate;

    /* ...number of bytes in input/output buffer */
    UWORD32                 buffer_size;

    /* ... input port parameters */
    /* ...input buffers */
    void               	    *input[XA_MIMO_IN_PORTS];

    UWORD32                 consumed[XA_MIMO_IN_PORTS];

    /* ...number of valid samples in individual buffers */
    UWORD32                 input_length[XA_MIMO_IN_PORTS];

    /* ...number of input ports */
    UWORD32                 num_in_ports;


    /* ... output port parameters */

    /* ...master volume and individual track volumes*/
    UWORD32                 volume[XA_MIMO_IN_PORTS + 1];

    /* ...output buffer */
    void               	    *output[XA_MIMO_OUT_PORTS];

    UWORD32                 produced[XA_MIMO_OUT_PORTS];

    UWORD32                 num_out_ports; /* ... number of output ports active */

    /* ...scratch buffer pointer */
    void               	    *scratch;

    UWORD32		    scratch_size;

    UWORD32                 pcm_width;
    UWORD32                 channels;

    /* ...input over flag */
    UWORD32             input_over[XA_MIMO_IN_PORTS];

    WORD16		    port_state[XA_MIMO_IN_PORTS + XA_MIMO_OUT_PORTS];

}   XAPcmAec;

/****************************************************************************
 * Gain multiplier table
 ******************************************************************************/
/*

0db = 1

-6db = 0.50118723362727228500155418688495
-12db = 0.25118864315095801110850320677993
-18db = 0.12589254117941672104239541063958

6db = 1.9952623149688796013524553967395
12db = 3.9810717055349725077025230508775
18db = 7.9432823472428150206591828283639
*/

WORD16 pcm_gains_dB_aec[7] = {   0,   -6,  -12, -18,    6,    12,    18};    // in dB
WORD16 pcm_gains_aec[7]    = {4096, 2053, 1029, 516, 8173, 16306, 32536};    // Q12 format

#define MAX_16BIT (32767)
#define MIN_16BIT (-32768)

/*******************************************************************************
 * DSP functions
 ******************************************************************************/

/* ...aec preinitialization (default parameters) */
static inline void xa_aec_preinit(XAPcmAec *d)
{
    UWORD32     i;

    /* ...pre-configuration initialization; reset internal data */
    memset(d, 0, sizeof(*d));

    /* ...set default parameters */
    d->num_in_ports = XA_MIMO_IN_PORTS;
    d->num_out_ports = XA_MIMO_OUT_PORTS;

    d->pcm_width = XA_MIMO_CFG_DEFAULT_PCM_WIDTH;
    d->channels = XA_MIMO_CFG_DEFAULT_CHANNELS;
    d->frame_size = XA_MIMO_CFG_DEFAULT_FRAMESIZE;

    for (i = 0; i <= d->num_in_ports; i++)
    {
    /* ...set default volumes (last index is a master volume)*/
        d->volume[i] = ((1 << 12) << 16) | (1 << 12);
    }

    for (i = 0; i < d->num_in_ports; i++)
    {
        /* ...set input over by default until actual input arrives*/
        d->input_over[i] = 1;
    }
}

/* ...do mixing of stereo PCM-16 streams */
static XA_ERRORCODE xa_aec_do_execute_stereo_16bit(XAPcmAec *d)
{
    WORD16  *output = d->output[0];
    WORD16  *b[d->num_in_ports];
    UWORD16 v_l[d->num_in_ports];
    UWORD16 v_r[d->num_in_ports];
    UWORD16 w_l, w_r;
    UWORD32 t32;
    UWORD32 i, j;
    UWORD32 ports_inactive;
    UWORD32 ports_completed;
    UWORD32 frame_size, inlen, sample_size, nout_remaining;
    UWORD32 inport_nodata_flag = 0;

    /* ...retrieve master volume - assume up to 24dB amplifying (4 bits) */
    t32 = d->volume[d->num_in_ports];
    w_l = (UWORD16)(t32 & 0xFFFF), w_r = (UWORD16)(t32 >> 16);

    XF_CHK_ERR(d->output[0], XA_MIMO_MIX_EXEC_FATAL_STATE);

    /* ...reset produced bytes */
    d->produced[0] = 0;

    for (i = 0; i < d->num_in_ports; i++)
    {
        d->consumed[i] = 0;
        if ((d->port_state[i] & XA_AEC_FLAG_PORT_CONNECTED) && (!d->input_over[i]) && (d->input_length[i] < d->buffer_size))
            inport_nodata_flag = 1; 
    }

    if (inport_nodata_flag)
    {
        return XA_MIMO_MIX_EXEC_NONFATAL_NO_DATA;
    }

    if((d->port_state[d->num_in_ports] & XA_AEC_FLAG_PORT_PAUSED))
    {
        /* non-fatal error if output port is paused */
        TRACE(PROCESS, _b("Port:%d is paused"), d->num_in_ports);
        return XA_MIMO_MIX_EXEC_NONFATAL_NO_DATA;
    }

    for (j = 0, ports_inactive = 0, ports_completed = 0; j < d->num_in_ports; j++)
    {
        /* ...check if we have input buffer available */
        if (d->input_length[j] == 0)
        {
            ports_inactive += (d->input_over[j]);

            /* ...set complete flag saying we have no active input port */
            if(ports_inactive == d->num_in_ports)
            {
                d->state |= XA_AEC_FLAG_EXEC_DONE;
                d->state &= ~XA_AEC_FLAG_OUTPUT;

                TRACE(PROCESS, _b("exec-done"));

                return XA_NO_ERROR;
            }
        }
        /* if any port is active, break */
        ports_completed += (d->input_over[j]);
    }
    TRACE(PROCESS, _b("ports_inactive:%u completed:%d"), ports_inactive, ports_completed);

    /* ...copy of input-bytes, decremented on consumption over iterations */
    memset(d->consumed, 0, sizeof(d->consumed));
    nout_remaining = (d->buffer_size - d->produced[0]);
    sample_size = ((d->pcm_width == 16) ? sizeof(WORD16) : sizeof(WORD32)) * d->channels;

    {
        WORD32 inports_active = d->num_in_ports;

        /* ...process all tracks, iterate only on unequal samples at input ports */
        inlen = d->buffer_size;

        /* ...prepare individual tracks */
        for (j = 0; j < d->num_in_ports; j++)
        {
            UWORD32 n = d->input_length[j];

            /* ...check if we have input buffer available */
            if (n)
            {
                /* ...assign input buffer pointer. */
                XF_CHK_ERR(b[j] = d->input[j], XA_MIMO_MIX_EXEC_FATAL_STATE);

                /* ...set individual track volume/balance */
                t32 = d->volume[j];
                v_l[j] = (UWORD16)(t32 & 0xFFFF), v_r[j] = (UWORD16)(t32 >> 16);

                /* ...advance the input pointer by sample-offset, required for unequal length input */
                b[j] += (d->consumed[j] / ((d->pcm_width == 16) ? sizeof(WORD16) : sizeof(WORD32)));

                /* ...input_size: bytes to process is the minimum of of all input port bytes */
                inlen = (inlen < n) ? inlen : n;
                /* ...output_size: bytes to process is at-most output port bytes */
                inlen = (inlen < nout_remaining) ? inlen : nout_remaining;
            }
            else
            {
                /* ...output silence (multiply garbage in the scratch buffer by 0) */
                b[j] = d->scratch;
                v_l[j] = v_r[j] = 0;
                inports_active -= (d->input_over[j]);
            }

            TRACE(PROCESS, _b("b[%u] = %p%s"), j, b[j], (n == 0 ? " - scratch" : ""));
        }//for(;j;)

        inlen = (inports_active)?inlen:0;

        if(inlen)
        {
            frame_size = inlen / sample_size;

            for (i=0; i < frame_size; i++)
            {
                WORD32 l32 = 0, r32 = 0;

                /* ...fill-in every channel in our map (unrolls loop here) */
                for (j = 0; j < d->num_in_ports; j++)
                {
                    /* ...left channel processing (no saturation here yet) */
                    l32 += *b[j]++ * v_l[j];

                    /* ...right channel processing */
                    r32 += *b[j]++ * v_r[j];
                }//for(;j;)

                /* ...normalize (truncate towards -inf) and multiply by master volume */
                l32 = ((l32 >> 12) * w_l) >> 12;
                r32 = ((r32 >> 12) * w_r) >> 12;

                /* ...saturate and store in buffer */
                *output++ = DSP_SATURATE_S16(l32);
                *output++ = DSP_SATURATE_S16(r32);
            }//for(;i;)

            for (j = 0; j < d->num_in_ports; j++)
            {
                if(d->input_length[j])
                {
                    d->input_length[j] -= frame_size * sample_size;
                    d->consumed[j] += frame_size * sample_size;

                    /* ...partial samples should be left unprocessed */
                    if((WORD32)d->input_length[j] < sample_size)
                    {
                        /* ...update partial bytes as consumed */
                        d->consumed[j] += d->input_length[j];
                        d->input_length[j] = 0;
                    }
                }
            }//for(;j;)
            d->produced[0] += frame_size * sample_size;
        }//if(inlen)
    }

    /* ...put flag saying we have output buffer */
    d->state |= XA_AEC_FLAG_OUTPUT;

    TRACE(PROCESS, _b("produced: %u bytes (%u samples)"), d->produced[0], ((d->pcm_width==16)?(d->produced[0]>>1):(d->produced[0]>>2)));

    /* ...set complete flag saying we have consumed all available input and input is over */
    if(ports_completed == d->num_in_ports)
    {
        d->state |= XA_AEC_FLAG_EXEC_DONE;

        TRACE(PROCESS, _b("exec-done"));
    }
    
    /* ...return success result code */
    return XA_NO_ERROR;
}

/* ...runtime reset */
static XA_ERRORCODE xa_aec_do_runtime_init(XAPcmAec *d)
{
    /* ...no special processing is needed here */
    return XA_NO_ERROR;
}

/*******************************************************************************
 * Commands processing
 ******************************************************************************/
/* ...codec API size query */
static XA_ERRORCODE xa_aec_get_api_size(XAPcmAec *d, WORD32 i_idx, pVOID pv_value)
{
    /* ...check parameters are sane */
    XF_CHK_ERR(pv_value, XA_API_FATAL_INVALID_CMD_TYPE);
    /* ...retrieve API structure size */
    *(WORD32 *)pv_value = sizeof(*d);

    return XA_NO_ERROR;
}

/* ...standard codec initialization routine */
static XA_ERRORCODE xa_aec_init(XAPcmAec *d, WORD32 i_idx, pVOID pv_value)
{
    /* ...sanity check - aec must be valid */
    XF_CHK_ERR(d, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...process particular initialization type */
    switch (i_idx)
    {
    case XA_CMD_TYPE_INIT_API_PRE_CONFIG_PARAMS:
    {
        /* ...pre-configuration initialization; reset internal data */
        xa_aec_preinit(d);

        /* ...and mark aec has been created */
        d->state = XA_AEC_FLAG_PREINIT_DONE;

        return XA_NO_ERROR;
    }

    case XA_CMD_TYPE_INIT_API_POST_CONFIG_PARAMS:
    {
        /* ...post-configuration initialization (all parameters are set) */
        XF_CHK_ERR(d->state & XA_AEC_FLAG_PREINIT_DONE, XA_API_FATAL_INVALID_CMD_TYPE);

        /* ...calculate input/output buffer size in bytes */
        d->buffer_size = d->channels * d->frame_size * (d->pcm_width == 16 ? sizeof(WORD16) : sizeof(WORD32));
        d->scratch_size = d->buffer_size;

        /* ...mark post-initialization is complete */
        d->state |= XA_AEC_FLAG_POSTINIT_DONE;

        return XA_NO_ERROR;
    }

    case XA_CMD_TYPE_INIT_PROCESS:
    {
        /* ...kick run-time initialization process; make sure aec is setup */
        XF_CHK_ERR(d->state & XA_AEC_FLAG_POSTINIT_DONE, XA_API_FATAL_INVALID_CMD_TYPE);

        /* ...enter into execution stage */
        d->state |= XA_AEC_FLAG_RUNNING;

        return XA_NO_ERROR;
    }

    case XA_CMD_TYPE_INIT_DONE_QUERY:
    {
        /* ...check if initialization is done; make sure pointer is sane */
        XF_CHK_ERR(pv_value, XA_API_FATAL_INVALID_CMD_TYPE);

        /* ...put current status */
        *(WORD32 *)pv_value = (d->state & XA_AEC_FLAG_RUNNING ? 1 : 0);

        return XA_NO_ERROR;
    }

    default:
        /* ...unrecognized command type */
        TRACE(ERROR, _x("Unrecognized command type: %X"), i_idx);
        return XA_API_FATAL_INVALID_CMD_TYPE;
    }
}

/* ...set aec configuration parameter */
static XA_ERRORCODE xa_aec_set_config_param(XAPcmAec *d, WORD32 i_idx, pVOID pv_value)
{
    UWORD32     i_value;

    /* ...sanity check - aec pointer must be sane */
    XF_CHK_ERR(d && pv_value, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...pre-initialization must be completed, aec must be idle */
    XF_CHK_ERR(d->state & XA_AEC_FLAG_PREINIT_DONE, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...get parameter value  */
    i_value = (UWORD32) *(WORD32 *)pv_value;

    /* ...process individual configuration parameter */
    switch (i_idx & 0xF)
    {
    case XA_MIMO_MIX_CONFIG_PARAM_SAMPLE_RATE:
         {
            /* ...set aec sample rate */
            switch((UWORD32)i_value)
            {
                case 4000:
                case 8000:
                case 11025:
                case 12000:
                case 16000:
                case 22050:
                case 24000:
                case 32000:
                case 44100:
                case 48000:
                case 64000:
                case 88200:
                case 96000:
                case 128000:
                case 176400:
                case 192000:
                    d->sample_rate = (UWORD32)i_value;
                    break;
                default:
                    XF_CHK_ERR(0, XA_MIMO_MIX_CONFIG_FATAL_RANGE);
            }
	break;
        }

    case XA_MIMO_MIX_CONFIG_PARAM_PCM_WIDTH:
        /* ...check value is permitted (16 bits only) */
        XF_CHK_ERR(i_value == 16, XA_MIMO_MIX_CONFIG_FATAL_RANGE);
        d->pcm_width = (UWORD32)i_value;
	break;

    case XA_MIMO_MIX_CONFIG_PARAM_CHANNELS:
        /* ...allow stereo only */
        XF_CHK_ERR(i_value == 2, XA_MIMO_MIX_CONFIG_FATAL_RANGE);
        d->channels = (UWORD32)i_value;
	break;

    case XA_MIMO_MIX_CONFIG_PARAM_PORT_PAUSE:
        {
          XF_CHK_ERR((i_value < (d->num_in_ports + d->num_out_ports)), XA_MIMO_MIX_CONFIG_FATAL_RANGE);
          d->port_state[i_value] |= XA_AEC_FLAG_PORT_PAUSED;
          TRACE(PROCESS, _b("Pause port:%d"), i_value);
        }
	break;

    case XA_MIMO_MIX_CONFIG_PARAM_PORT_RESUME:
        {
          XF_CHK_ERR((i_value < (d->num_in_ports + d->num_out_ports)), XA_MIMO_MIX_CONFIG_FATAL_RANGE);
          d->port_state[i_value] &= ~XA_AEC_FLAG_PORT_PAUSED;
          TRACE(PROCESS, _b("Resume port:%d"), i_value);
        }
	break;

    case XA_MIMO_MIX_CONFIG_PARAM_PORT_CONNECT:
        {
          XF_CHK_ERR((i_value < (d->num_in_ports + d->num_out_ports)), XA_MIMO_MIX_CONFIG_FATAL_RANGE);
          d->port_state[i_value] |= XA_AEC_FLAG_PORT_CONNECTED;
          TRACE(PROCESS, _b("Connect on port:%d"), i_value);
        }
	break;

    case XA_MIMO_MIX_CONFIG_PARAM_PORT_DISCONNECT:
        {
          XF_CHK_ERR((i_value < (d->num_in_ports + d->num_out_ports)), XA_MIMO_MIX_CONFIG_FATAL_RANGE);
          d->port_state[i_value] &= ~XA_AEC_FLAG_PORT_CONNECTED;
          d->input_over[i_value] = 1; /* ...on a disconnect, mark input_over for the port (TENA-4266). */
          TRACE(PROCESS, _b("Disconnect on port:%d"), i_value);
        }
	break;

    default:
        TRACE(ERROR, _x("Invalid parameter: %X"), i_idx);
        return XA_API_FATAL_INVALID_CMD_TYPE;
    }

    return XA_NO_ERROR;
}

/* ...retrieve configuration parameter */
static XA_ERRORCODE xa_aec_get_config_param(XAPcmAec *d, WORD32 i_idx, pVOID pv_value)
{
    /* ...sanity check - aec must be initialized */
    XF_CHK_ERR(d && pv_value, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...make sure pre-initialization is completed */
    XF_CHK_ERR(d->state & XA_AEC_FLAG_PREINIT_DONE, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...process individual configuration parameter */
    switch (i_idx & 0xF)
    {
    case XA_MIMO_MIX_CONFIG_PARAM_SAMPLE_RATE:
        /* ...return aec sample rate */
        *(WORD32 *)pv_value = d->sample_rate;
        return XA_NO_ERROR;

    case XA_MIMO_MIX_CONFIG_PARAM_PCM_WIDTH:
        /* ...return current PCM width */
        *(WORD32 *)pv_value = d->pcm_width;
        return XA_NO_ERROR;

    case XA_MIMO_MIX_CONFIG_PARAM_CHANNELS:
        /* ...return current channel number */
        *(WORD32 *)pv_value = d->channels;
        return XA_NO_ERROR;

    default:
        TRACE(ERROR, _x("Invalid parameter: %X"), i_idx);
        return XA_API_FATAL_INVALID_CMD_TYPE;
    }
}

/* ...execution command */
static XA_ERRORCODE xa_aec_execute(XAPcmAec *d, WORD32 i_idx, pVOID pv_value)
{
    XA_ERRORCODE ret;
#ifdef XAF_PROFILE
    clk_t mimo_mix_start, mimo_mix_stop;
#endif

    /* ...sanity check - aec must be valid */
    XF_CHK_ERR(d, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...aec must be in running state */
    XF_CHK_ERR(d->state & XA_AEC_FLAG_RUNNING, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...process individual command type */
    switch (i_idx)
    {
    case XA_CMD_TYPE_DO_EXECUTE:
        /* ...perform pcm-gain of the channels */
#ifdef XAF_PROFILE
        mimo_mix_start = clk_read_start(CLK_SELN_THREAD);
#endif
        ret = xa_aec_do_execute_stereo_16bit(d);
#ifdef XAF_PROFILE
        mimo_mix_stop = clk_read_stop(CLK_SELN_THREAD);
        mimo_mix_cycles += clk_diff(mimo_mix_stop, mimo_mix_start);
#endif
        return ret;

    case XA_CMD_TYPE_DONE_QUERY:
        /* ...check if processing is complete */
        XF_CHK_ERR(pv_value, XA_API_FATAL_INVALID_CMD_TYPE);
        *(WORD32 *)pv_value = (d->state & XA_AEC_FLAG_EXEC_DONE? 1 : 0);
        return XA_NO_ERROR;

    case XA_CMD_TYPE_DO_RUNTIME_INIT:
        /* ...reset aec operation */
        return xa_aec_do_runtime_init(d);

    default:
        /* ...unrecognized command */
        TRACE(ERROR, _x("Invalid index: %X"), i_idx);
        return XA_API_FATAL_INVALID_CMD_TYPE;
    }
}

/* ...set number of input bytes */
static XA_ERRORCODE xa_aec_set_input_bytes(XAPcmAec *d, WORD32 i_idx, pVOID pv_value)
{
    UWORD32     size;

    /* ...sanity check - check parameters */
    XF_CHK_ERR(d && pv_value, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...track index must be valid */
    XF_CHK_ERR(i_idx >= 0 && i_idx < d->num_in_ports, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...aec must be initialized */
    XF_CHK_ERR(d->state & XA_AEC_FLAG_POSTINIT_DONE, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...input buffer must exist */
    XF_CHK_ERR(d->input[i_idx], XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...input frame length should not be zero (in bytes) */
    XF_CHK_ERR((size = (UWORD32)*(WORD32 *)pv_value) >= 0, XA_MIMO_MIX_CONFIG_FATAL_RANGE);

    /* ...all is correct; set input buffer length in bytes */
    d->input_length[i_idx] = size;

    if(size)
    {
        /* ...reset input_over flag when data arrives */
        d->input_over[i_idx] = 0;
    }

    return XA_NO_ERROR;
}

/* ...get number of output bytes */
static XA_ERRORCODE xa_aec_get_output_bytes(XAPcmAec *d, WORD32 i_idx, pVOID pv_value)
{
    int p_idx;
    /* ...sanity check - check parameters */
    XF_CHK_ERR(d && pv_value, XA_API_FATAL_INVALID_CMD_TYPE);

    p_idx = i_idx-d->num_in_ports;
    /* ...track index must be zero */
    XF_CHK_ERR((p_idx >= 0) && (p_idx < d->num_out_ports), XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...aec must be running */
    XF_CHK_ERR(d->state & XA_AEC_FLAG_RUNNING, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...output buffer must exist */
    XF_CHK_ERR(d->output, XA_MIMO_MIX_EXEC_FATAL_STATE);

    /* ...return number of produced bytes */
    *(WORD32 *)pv_value = (d->state & XA_AEC_FLAG_OUTPUT ? d->produced[p_idx] : 0);

    return XA_NO_ERROR;
}

/* ...get number of consumed bytes */
static XA_ERRORCODE xa_aec_get_curidx_input_buf(XAPcmAec *d, WORD32 i_idx, pVOID pv_value)
{
    /* ...sanity check - check parameters */
    XF_CHK_ERR(d && pv_value, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...track index must be valid */
    XF_CHK_ERR(i_idx >= 0 && i_idx < XA_MIMO_IN_PORTS, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...aec must be running */
    XF_CHK_ERR(d->state & XA_AEC_FLAG_RUNNING, XA_MIMO_MIX_EXEC_FATAL_STATE);

    /* ...input buffer must exist */
    XF_CHK_ERR(d->input[i_idx], XA_MIMO_MIX_EXEC_FATAL_STATE);

    /* ...return number of bytes consumed (always consume fixed-length chunk) */
    *(WORD32 *)pv_value = d->consumed[i_idx];

    return XA_NO_ERROR;
}

/* ...end-of-stream processing */
static XA_ERRORCODE xa_aec_input_over(XAPcmAec *d, WORD32 i_idx, pVOID pv_value)
{
    //int i, input_over_count = 0;

    /* ...sanity check */
    XF_CHK_ERR(d, XA_API_FATAL_INVALID_CMD_TYPE);

    d->input_over[i_idx] = 1;

    /* ... port specific end-of-stream flag */
    d->port_state[i_idx] |= XA_AEC_FLAG_COMPLETE;
#if 0
    for(i=0; i<d->num_in_ports; i++)
    {
    	input_over_count += (XA_AEC_FLAG_COMPLETE & d->port_state[i])?1:0;
    }
#endif


    TRACE(PROCESS, _b("Input-over-condition signalled for port %d"), i_idx);

    return XA_NO_ERROR;
}

/*******************************************************************************
 * Memory information API
 ******************************************************************************/

/* ..get total amount of data for memory tables */
static XA_ERRORCODE xa_aec_get_memtabs_size(XAPcmAec *d, WORD32 i_idx, pVOID pv_value)
{
    /* ...basic sanity checks */
    XF_CHK_ERR(d && pv_value, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...check aec is pre-initialized */
    XF_CHK_ERR(d->state & XA_AEC_FLAG_PREINIT_DONE, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...we have all our tables inside API structure - good? tbd */
    *(WORD32 *)pv_value = 0;

    return XA_NO_ERROR;
}

/* ..set memory tables pointer */
static XA_ERRORCODE xa_aec_set_memtabs_ptr(XAPcmAec *d, WORD32 i_idx, pVOID pv_value)
{
    /* ...basic sanity checks */
    XF_CHK_ERR(d && pv_value, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...check aec is pre-initialized */
    XF_CHK_ERR(d->state & XA_AEC_FLAG_PREINIT_DONE, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...do not do anything; just return success - tbd */
    return XA_NO_ERROR;
}

/* ...return total amount of memory buffers */
static XA_ERRORCODE xa_aec_get_n_memtabs(XAPcmAec *d, WORD32 i_idx, pVOID pv_value)
{
    /* ...basic sanity checks */
    XF_CHK_ERR(d && pv_value, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...we have N input ports, M output ports and 1 scratch buffer */
    *(WORD32 *)pv_value = d->num_in_ports + d->num_out_ports + 1;

    return XA_NO_ERROR;
}

/* ...return memory buffer data */
static XA_ERRORCODE xa_aec_get_mem_info_size(XAPcmAec *d, WORD32 i_idx, pVOID pv_value)
{
    /* ...basic sanity check */
    XF_CHK_ERR(d && pv_value, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...return frame buffer minimal size only after post-initialization is done */
    XF_CHK_ERR(d->state & XA_AEC_FLAG_POSTINIT_DONE, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...all buffers are of the same length */
    WORD32 n_mems = (d->num_in_ports + d->num_out_ports + 1);
    if(i_idx < d->num_in_ports)
    {
        /* ...input buffers */
        *(WORD32 *)pv_value = d->buffer_size;
    }
    else
    if(i_idx < (d->num_in_ports + d->num_out_ports))
    {
        /* ...output buffer */
        *(WORD32 *)pv_value = d->buffer_size;
    }
    else
    if(i_idx < n_mems)
    {
        /* ...scratch buffer */
        *(WORD32 *)pv_value = d->scratch_size;
    }
    else{
	    return XA_API_FATAL_INVALID_CMD_TYPE;
    }

    return XA_NO_ERROR;
}

/* ...return memory alignment data */
static XA_ERRORCODE xa_aec_get_mem_info_alignment(XAPcmAec *d, WORD32 i_idx, pVOID pv_value)
{
    /* ...basic sanity check */
    XF_CHK_ERR(d && pv_value, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...return frame buffer minimal size only after post-initialization is done */
    XF_CHK_ERR(d->state & XA_AEC_FLAG_POSTINIT_DONE, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...all buffers are 4-bytes aligned */
    *(WORD32 *)pv_value = 4;

    return XA_NO_ERROR;
}

/* ...return memory type data */
static XA_ERRORCODE xa_aec_get_mem_info_type(XAPcmAec *d, WORD32 i_idx, pVOID pv_value)
{
    /* ...basic sanity check */
    XF_CHK_ERR(d && pv_value, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...return frame buffer minimal size only after post-initialization is done */

    XF_CHK_ERR(d->state & XA_AEC_FLAG_POSTINIT_DONE, XA_API_FATAL_INVALID_CMD_TYPE);

    WORD32 n_mems = (d->num_in_ports + d->num_out_ports + 1);
    if(i_idx < d->num_in_ports)
    {
        /* ...input buffers */
        *(WORD32 *)pv_value = XA_MEMTYPE_INPUT;
    }
    else
    if(i_idx < (d->num_in_ports + d->num_out_ports))
    {
        /* ...output buffer */
        *(WORD32 *)pv_value = XA_MEMTYPE_OUTPUT;
    }
    else
    if(i_idx < n_mems)
    {
        /* ...scratch buffer */
        *(WORD32 *)pv_value = XA_MEMTYPE_SCRATCH;
    }
    else{
	    return XA_API_FATAL_INVALID_CMD_TYPE;
    }
    return XA_NO_ERROR;
}

/* ...set memory pointer */
static XA_ERRORCODE xa_aec_set_mem_ptr(XAPcmAec *d, WORD32 i_idx, pVOID pv_value)
{
    /* ...basic sanity check */
    XF_CHK_ERR(d && pv_value, XA_API_FATAL_INVALID_CMD_TYPE);

    /* ...codec must be initialized */
    XF_CHK_ERR(d->state & XA_AEC_FLAG_POSTINIT_DONE, XA_API_FATAL_INVALID_CMD_TYPE);

    WORD32 n_mems = (d->num_in_ports + d->num_out_ports + 1);
    if(i_idx < d->num_in_ports)
    {
        /* ...input buffers */
        d->input[i_idx] = pv_value;
    }
    else
    if(i_idx < (d->num_in_ports + d->num_out_ports))
    {
        /* ...output buffer */
        d->output[i_idx-d->num_in_ports] = pv_value;
    }
    else
    if(i_idx < n_mems)
    {
        /* ...scratch buffer */
        d->scratch = pv_value;
    }
    else{
	    return XA_API_FATAL_INVALID_CMD_TYPE;
    }
    return XA_NO_ERROR;
}

/*******************************************************************************
 * API command hooks
 ******************************************************************************/

static XA_ERRORCODE (* const xa_aec_api[])(XAPcmAec *, WORD32, pVOID) =
{
    [XA_API_CMD_GET_API_SIZE]           = xa_aec_get_api_size,

    [XA_API_CMD_INIT]                   = xa_aec_init,
    [XA_API_CMD_SET_CONFIG_PARAM]       = xa_aec_set_config_param,
    [XA_API_CMD_GET_CONFIG_PARAM]       = xa_aec_get_config_param,

    [XA_API_CMD_EXECUTE]                = xa_aec_execute,
    [XA_API_CMD_SET_INPUT_BYTES]        = xa_aec_set_input_bytes,
    [XA_API_CMD_GET_OUTPUT_BYTES]       = xa_aec_get_output_bytes,
    [XA_API_CMD_GET_CURIDX_INPUT_BUF]   = xa_aec_get_curidx_input_buf,
    [XA_API_CMD_INPUT_OVER]             = xa_aec_input_over,

    [XA_API_CMD_GET_MEMTABS_SIZE]       = xa_aec_get_memtabs_size,
    [XA_API_CMD_SET_MEMTABS_PTR]        = xa_aec_set_memtabs_ptr,
    [XA_API_CMD_GET_N_MEMTABS]          = xa_aec_get_n_memtabs,
    [XA_API_CMD_GET_MEM_INFO_SIZE]      = xa_aec_get_mem_info_size,
    [XA_API_CMD_GET_MEM_INFO_ALIGNMENT] = xa_aec_get_mem_info_alignment,
    [XA_API_CMD_GET_MEM_INFO_TYPE]      = xa_aec_get_mem_info_type,
    [XA_API_CMD_SET_MEM_PTR]            = xa_aec_set_mem_ptr,
};

/* ...total number of commands supported */
#define XA_AEC_API_COMMANDS_NUM   (sizeof(xa_aec_api) / sizeof(xa_aec_api[0]))

/*******************************************************************************
 * API entry point
 ******************************************************************************/

XA_ERRORCODE xa_mimo_mix4(xa_codec_handle_t p_xa_module_obj, WORD32 i_cmd, WORD32 i_idx, pVOID pv_value)
{
    XAPcmAec    *d = (XAPcmAec *) p_xa_module_obj;

    /* ...check if command index is sane */
    XF_CHK_ERR(i_cmd < XA_AEC_API_COMMANDS_NUM, XA_API_FATAL_INVALID_CMD);

    /* ...see if command is defined */
    XF_CHK_ERR(xa_aec_api[i_cmd], XA_API_FATAL_INVALID_CMD);

    /* ...execute requested command */
    return xa_aec_api[i_cmd](d, i_idx, pv_value);
}

