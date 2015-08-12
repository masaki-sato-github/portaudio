/*
 * $Id: pa_linux_alsa.c 1691 2011-05-26 20:19:19Z aknudsen $
 * PortAudio Portable Real-Time Audio Library
 * Latest Version at: http://www.portaudio.com
 * IOAUDIO implementation by Joshua Haberman and Arve Knudsen
 *
 * Copyright (c) 2002 Joshua Haberman <joshua@haberman.com>
 * Copyright (c) 2005-2009 Arve Knudsen <arve.knudsen@gmail.com>
 * Copyright (c) 2008 Kevin Kofler <kevin.kofler@chello.at>
 *
 * Based on the Open Source API proposed by Ross Bencina
 * Copyright (c) 1999-2002 Ross Bencina, Phil Burk
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * The text above constitutes the entire PortAudio license; however,
 * the PortAudio community also makes the following non-binding requests:
 *
 * Any person wishing to distribute modifications to the Software is
 * requested to send the modifications to the original developer so that
 * they can be incorporated into the canonical version. It is also
 * requested that these non-binding requests be included along with the
 * license above.
 */

/**
 @file
 @ingroup hostapi_src
*/

//!@todo ms enable buffer size calculation
//!@todo ms enable frames per buffer determination
//!@todo ms use plugin API for mmap'ed implementation (to match original mmap'ed implementation for ALSA) replace area related implementation.
//!@todo ms enable SetApproximateSampleRate
//!@todo ms enable GetExactSampleRate
//!@todo ms enable DoChannelAdaption?
//!@todo ms WriteStream, enable snd_pcm_start with snd_pcm_playback_go
#include <sys/asoundlib.h>

#include <sys/poll.h>
#include <string.h> /* strlen() */
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/mman.h>
#include <signal.h> /* For sig_atomic_t */
#include <stdlib.h> /* atoi */
#include <stdio.h> /* snprintf */
#include <errno.h>
#ifdef PA_IOAUDIO_DYNAMIC
    #include <dlfcn.h> /* For dlXXX functions */
#endif

#include "portaudio.h"
#include "pa_util.h"
#include "pa_unix_util.h"
#include "pa_allocation.h"
#include "pa_hostapi.h"
#include "pa_stream.h"
#include "pa_cpuload.h"
#include "pa_process.h"
#include "pa_endianness.h"
#include "pa_debugprint.h"

#include "pa_qnx_ioaudio.h"

#ifndef SND_PCM_TSTAMP_ENABLE
#define SND_PCM_TSTAMP_ENABLE SND_PCM_TSTAMP_MMAP
#endif

/* Defines ioaudio function types and pointers to these functions. */
#define _PA_DEFINE_FUNC(x)  typedef typeof(x) x##_ft; static x##_ft *ioaudio_##x = 0

/* Alloca helper. */
//#define __ioaudio_snd_alloca(ptr,type) do { size_t __ioaudio_alloca_size = ioaudio_##type##_sizeof(); (*ptr) = (type##_t *) alloca(__ioaudio_alloca_size); memset(*ptr, 0, __ioaudio_alloca_size); } while (0)

_PA_DEFINE_FUNC(snd_pcm_open);
_PA_DEFINE_FUNC(snd_pcm_open_name);
_PA_DEFINE_FUNC(snd_pcm_close);
_PA_DEFINE_FUNC(snd_pcm_nonblock_mode);
//_PA_DEFINE_FUNC(snd_pcm_frames_to_bytes);
_PA_DEFINE_FUNC(snd_pcm_channel_prepare);
//_PA_DEFINE_FUNC(snd_pcm_start);
//_PA_DEFINE_FUNC(snd_pcm_resume);
//_PA_DEFINE_FUNC(snd_pcm_wait);
_PA_DEFINE_FUNC(snd_pcm_channel_status);
//_PA_DEFINE_FUNC(snd_pcm_avail_update);
//_PA_DEFINE_FUNC(snd_pcm_areas_silence);
//_PA_DEFINE_FUNC(snd_pcm_mmap_begin);
//_PA_DEFINE_FUNC(snd_pcm_mmap_commit);
_PA_DEFINE_FUNC(snd_pcm_read);
//_PA_DEFINE_FUNC(snd_pcm_readn);
_PA_DEFINE_FUNC(snd_pcm_write);
//_PA_DEFINE_FUNC(snd_pcm_writen);
_PA_DEFINE_FUNC(snd_pcm_playback_drain);
//_PA_DEFINE_FUNC(snd_pcm_recover);
_PA_DEFINE_FUNC(snd_pcm_channel_flush);
//_PA_DEFINE_FUNC(snd_pcm_area_copy);
_PA_DEFINE_FUNC(snd_pcm_file_descriptor);
//_PA_DEFINE_FUNC(snd_pcm_poll_descriptors_count);
//_PA_DEFINE_FUNC(snd_pcm_poll_descriptors_revents);
_PA_DEFINE_FUNC(snd_pcm_format_size);
_PA_DEFINE_FUNC(snd_pcm_link);
//_PA_DEFINE_FUNC(snd_pcm_delay);

_PA_DEFINE_FUNC(snd_pcm_channel_params);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_sizeof);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_malloc);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_free);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_any);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_set_access);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_set_format);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_set_channels);
////_PA_DEFINE_FUNC(snd_pcm_hw_params_set_periods_near);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_set_rate_near); //!!!
//_PA_DEFINE_FUNC(snd_pcm_hw_params_set_rate);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_set_rate_resample);
////_PA_DEFINE_FUNC(snd_pcm_hw_params_set_buffer_time_near);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_set_buffer_size);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_set_buffer_size_near); //!!!
//_PA_DEFINE_FUNC(snd_pcm_hw_params_set_buffer_size_min);
////_PA_DEFINE_FUNC(snd_pcm_hw_params_set_period_time_near);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_set_period_size_near);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_set_periods_integer);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_set_periods_min);

_PA_DEFINE_FUNC(snd_pcm_channel_setup);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_get_buffer_size);
////_PA_DEFINE_FUNC(snd_pcm_hw_params_get_period_size);
////_PA_DEFINE_FUNC(snd_pcm_hw_params_get_access);
////_PA_DEFINE_FUNC(snd_pcm_hw_params_get_periods);
////_PA_DEFINE_FUNC(snd_pcm_hw_params_get_rate);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_get_channels_min);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_get_channels_max);

//_PA_DEFINE_FUNC(snd_pcm_channel_test_period_size);
//_PA_DEFINE_FUNC(snd_pcm_channel_test_format);
//_PA_DEFINE_FUNC(snd_pcm_channel_test_access);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_dump);
//_PA_DEFINE_FUNC(snd_pcm_hw_params);

//_PA_DEFINE_FUNC(snd_pcm_hw_params_get_periods_min);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_get_periods_max);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_set_period_size);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_get_period_size_min);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_get_period_size_max);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_get_buffer_size_max);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_get_rate_min);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_get_rate_max);
//_PA_DEFINE_FUNC(snd_pcm_hw_params_get_rate_numden);
//#define ioaudio_snd_pcm_hw_params_alloca(ptr) __ioaudio_snd_alloca(ptr, snd_pcm_hw_params)

//_PA_DEFINE_FUNC(snd_pcm_sw_params_sizeof);
//_PA_DEFINE_FUNC(snd_pcm_sw_params_malloc);
//_PA_DEFINE_FUNC(snd_pcm_sw_params_current);
//_PA_DEFINE_FUNC(snd_pcm_sw_params_set_avail_min);
//_PA_DEFINE_FUNC(snd_pcm_sw_params);
//_PA_DEFINE_FUNC(snd_pcm_sw_params_free);
//_PA_DEFINE_FUNC(snd_pcm_sw_params_set_start_threshold);
//_PA_DEFINE_FUNC(snd_pcm_sw_params_set_stop_threshold);
//_PA_DEFINE_FUNC(snd_pcm_sw_params_get_boundary);
//_PA_DEFINE_FUNC(snd_pcm_sw_params_set_silence_threshold);
//_PA_DEFINE_FUNC(snd_pcm_sw_params_set_silence_size);
//_PA_DEFINE_FUNC(snd_pcm_sw_params_set_xfer_align);
//_PA_DEFINE_FUNC(snd_pcm_sw_params_set_tstamp_mode);
//#define ioaudio_snd_pcm_sw_params_alloca(ptr) __ioaudio_snd_alloca(ptr, snd_pcm_sw_params)

//_PA_DEFINE_FUNC(snd_pcm_hw_info);
_PA_DEFINE_FUNC(snd_pcm_info);
_PA_DEFINE_FUNC(snd_pcm_channel_info);
//_PA_DEFINE_FUNC(snd_pcm_info_sizeof);
//_PA_DEFINE_FUNC(snd_pcm_info_malloc);
//_PA_DEFINE_FUNC(snd_pcm_info_free);
//_PA_DEFINE_FUNC(snd_pcm_info_set_device);
//_PA_DEFINE_FUNC(snd_pcm_info_set_subdevice);
//_PA_DEFINE_FUNC(snd_pcm_info_set_stream);
//_PA_DEFINE_FUNC(snd_pcm_info_get_name);
//#define ioaudio_snd_pcm_info_alloca(ptr) __ioaudio_snd_alloca(ptr, snd_pcm_info)
_PA_DEFINE_FUNC(snd_card_name);
_PA_DEFINE_FUNC(snd_cards_list);

//_PA_DEFINE_FUNC(snd_ctl_pcm_next_device);
_PA_DEFINE_FUNC(snd_ctl_pcm_info);
_PA_DEFINE_FUNC(snd_ctl_open);
_PA_DEFINE_FUNC(snd_ctl_hw_info);
//_PA_DEFINE_FUNC(snd_ctl_close);
//_PA_DEFINE_FUNC(snd_ctl_card_info_malloc);
//_PA_DEFINE_FUNC(snd_ctl_card_info_free);
//_PA_DEFINE_FUNC(snd_ctl_card_info);
//_PA_DEFINE_FUNC(snd_ctl_card_info_sizeof);
//_PA_DEFINE_FUNC(snd_ctl_card_info_get_name);
//#define ioaudio_snd_ctl_card_info_alloca(ptr) __ioaudio_snd_alloca(ptr, snd_ctl_card_info)

//_PA_DEFINE_FUNC(snd_config);
//_PA_DEFINE_FUNC(snd_config_update);
//_PA_DEFINE_FUNC(snd_config_search);
//_PA_DEFINE_FUNC(snd_config_iterator_entry);
//_PA_DEFINE_FUNC(snd_config_iterator_first);
//_PA_DEFINE_FUNC(snd_config_iterator_end);
//_PA_DEFINE_FUNC(snd_config_iterator_next);
//_PA_DEFINE_FUNC(snd_config_get_string);
//_PA_DEFINE_FUNC(snd_config_get_id);
//_PA_DEFINE_FUNC(snd_config_update_free_global);

//_PA_DEFINE_FUNC(snd_pcm_status);
//_PA_DEFINE_FUNC(snd_pcm_status_sizeof);
//_PA_DEFINE_FUNC(snd_pcm_status_get_tstamp);
//_PA_DEFINE_FUNC(snd_pcm_status_get_state);
//_PA_DEFINE_FUNC(snd_pcm_status_get_trigger_tstamp);
//_PA_DEFINE_FUNC(snd_pcm_status_get_delay);
//#define ioaudio_snd_pcm_status_alloca(ptr) __ioaudio_snd_alloca(ptr, snd_pcm_status)

//_PA_DEFINE_FUNC(snd_card_next);
_PA_DEFINE_FUNC(snd_strerror);
//_PA_DEFINE_FUNC(snd_output_stdio_attach);

#define ioaudio_snd_config_for_each(pos, next, node)\
    for (pos = ioaudio_snd_config_iterator_first(node),\
         next = ioaudio_snd_config_iterator_next(pos);\
         pos != ioaudio_snd_config_iterator_end(node); pos = next, next = ioaudio_snd_config_iterator_next(pos))

#undef _PA_DEFINE_FUNC

#ifdef PA_IOAUDIO_DYNAMIC

/* Redefine 'PA_IOAUDIO_PATHNAME' to a different ioaudio library name if desired. */
#ifndef PA_IOAUDIO_PATHNAME
    #define PA_IOAUDIO_PATHNAME "libasound.so"
#endif
static const char *g_IoaudioLibName = PA_IOAUDIO_PATHNAME;

/* Handle to dynamically loaded library. */
static void *g_IoaudioLib = NULL;

#define _PA_LOCAL_IMPL(x) __pa_local_##x

#if 0
int _PA_LOCAL_IMPL(snd_pcm_hw_params_set_rate_near) (snd_pcm_t *pcm, snd_pcm_channel_params_t *params, unsigned int *val, int *dir)
{
	int ret;

	if ((ret = ioaudio_snd_pcm_hw_params_set_rate(pcm, params, (*val), (*dir))) < 0)
		return ret;

	return 0;
}

int _PA_LOCAL_IMPL(snd_pcm_hw_params_set_buffer_size_near) (snd_pcm_t *pcm, snd_pcm_channel_params_t *params, snd_pcm_uframes_t *val)
{
	int ret;

	if ((ret = ioaudio_snd_pcm_hw_params_set_buffer_size(pcm, params, (*val))) < 0)
		return ret;

	return 0;
}

int _PA_LOCAL_IMPL(snd_pcm_hw_params_set_period_size_near) (snd_pcm_t *pcm, snd_pcm_channel_params_t *params, snd_pcm_uframes_t *val, int *dir)
{
	int ret;

	if ((ret = ioaudio_snd_pcm_hw_params_set_period_size(pcm, params, (*val), (*dir))) < 0)
		return ret;

	return 0;
}

int _PA_LOCAL_IMPL(snd_pcm_hw_params_get_channels_min) (const snd_pcm_channel_params_t *params, unsigned int *val)
{
	(*val) = 1;
	return 0;
}

int _PA_LOCAL_IMPL(snd_pcm_hw_params_get_channels_max) (const snd_pcm_channel_params_t *params, unsigned int *val)
{
	(*val) = 2;
	return 0;
}

int _PA_LOCAL_IMPL(snd_pcm_hw_params_get_periods_min) (const snd_pcm_channel_params_t *params, unsigned int *val, int *dir)
{
	(*val) = 2;
	return 0;
}

int _PA_LOCAL_IMPL(snd_pcm_hw_params_get_periods_max) (const snd_pcm_channel_params_t *params, unsigned int *val, int *dir)
{
	(*val) = 8;
	return 0;
}

int _PA_LOCAL_IMPL(snd_pcm_hw_params_get_period_size_min) (const snd_pcm_channel_params_t *params, snd_pcm_uframes_t *frames, int *dir)
{
	(*frames) = 64;
	return 0;
}

int _PA_LOCAL_IMPL(snd_pcm_hw_params_get_period_size_max) (const snd_pcm_channel_params_t *params, snd_pcm_uframes_t *frames, int *dir)
{
	(*frames) = 512;
	return 0;
}

int _PA_LOCAL_IMPL(snd_pcm_hw_params_get_buffer_size_max) (const snd_pcm_channel_params_t *params, snd_pcm_uframes_t *val)
{
	int ret;
	int dir                = 0;
	snd_pcm_uframes_t pmax = 0;
	unsigned int      pcnt = 0;

	if ((ret = _PA_LOCAL_IMPL(snd_pcm_hw_params_get_period_size_max)(params, &pmax, &dir)) < 0)
		return ret;
	if ((ret = _PA_LOCAL_IMPL(snd_pcm_hw_params_get_periods_max)(params, &pcnt, &dir)) < 0)
		return ret;

	(*val) = pmax * pcnt;
	return 0;
}

int _PA_LOCAL_IMPL(snd_pcm_hw_params_get_rate_min) (const snd_pcm_channel_params_t *params, unsigned int *val, int *dir)
{
	(*val) = 44100;
	return 0;
}

int _PA_LOCAL_IMPL(snd_pcm_hw_params_get_rate_max) (const snd_pcm_channel_params_t *params, unsigned int *val, int *dir)
{
	(*val) = 44100;
	return 0;
}
#endif

#endif // PA_IOAUDIO_DYNAMIC

// non-existing types defined for QNX
typedef unsigned long snd_pcm_uframes_t;
typedef long snd_pcm_sframes_t;
typedef uint32_t snd_pcm_format_format_t;
typedef struct snd_pcm_channel_area
{
    void* addr;
    unsigned int first;
    unsigned int step;
} snd_pcm_channel_area_t;
typedef struct timeval snd_timestamp_t;
typedef enum
{
    false = 0,
    true = 1
} bool;

#define 	__snd_alloca(ptr, type)   do { *ptr = (type##_t *) alloca(type##_sizeof()); memset(*ptr, 0, type##_sizeof()); } while (0)

#define ioaudio_snd_pcm_hw_params_alloca __snd_alloca(ptr, snd_pcm_hw_params)
// end non-existing types defined for QNX

/* Trying to load ioaudio library dynamically if 'PA_IOAUDIO_DYNAMIC' is defined, othervise
   will link during compilation.
*/
static int PaIoaudio_LoadLibrary()
{
#ifdef PA_IOAUDIO_DYNAMIC

	PA_DEBUG(( "%s: loading IOAUDIO library file - %s\n", __FUNCTION__, g_IoaudioLibName ));

	dlerror();
    g_IoaudioLib = dlopen(g_IoaudioLibName, (RTLD_NOW|RTLD_GLOBAL));
    if (g_IoaudioLib == NULL)
    {
    	PA_DEBUG(( "%s: failed dlopen() IOAUDIO library file - %s, error: %s\n", __FUNCTION__, g_IoaudioLibName, dlerror() ));
    	return 0;
    }

    PA_DEBUG(( "%s: loading IOAUDIO API\n", __FUNCTION__ ));

    #define _PA_LOAD_FUNC(x) do {             \
		ioaudio_##x = dlsym(g_IoaudioLib, #x);      \
		if (ioaudio_##x == NULL) {               \
			PA_DEBUG(( "%s: symbol [%s] not found in - %s, error: %s\n", __FUNCTION__, #x, g_IoaudioLibName, dlerror() )); }\
        } while(0)

#else

    #define _PA_LOAD_FUNC(x) ioaudio_##x = &x

#endif

_PA_LOAD_FUNC(snd_pcm_open);
_PA_LOAD_FUNC(snd_pcm_open_name);
_PA_LOAD_FUNC(snd_pcm_close);
_PA_LOAD_FUNC(snd_pcm_nonblock_mode);
//_PA_LOAD_FUNC(snd_pcm_frames_to_bytes);
_PA_LOAD_FUNC(snd_pcm_channel_prepare);
//_PA_LOAD_FUNC(snd_pcm_start);
//_PA_LOAD_FUNC(snd_pcm_resume);
//_PA_LOAD_FUNC(snd_pcm_wait);
_PA_LOAD_FUNC(snd_pcm_channel_status);
//_PA_LOAD_FUNC(snd_pcm_avail_update);
//_PA_LOAD_FUNC(snd_pcm_areas_silence);
//_PA_LOAD_FUNC(snd_pcm_mmap_begin);
//_PA_LOAD_FUNC(snd_pcm_mmap_commit);
_PA_LOAD_FUNC(snd_pcm_read);
//_PA_LOAD_FUNC(snd_pcm_readn);
_PA_LOAD_FUNC(snd_pcm_write);
//_PA_LOAD_FUNC(snd_pcm_writen);
_PA_LOAD_FUNC(snd_pcm_playback_drain);
//_PA_LOAD_FUNC(snd_pcm_recover);
_PA_LOAD_FUNC(snd_pcm_channel_flush);
//_PA_LOAD_FUNC(snd_pcm_area_copy);
_PA_LOAD_FUNC(snd_pcm_file_descriptor);
//_PA_LOAD_FUNC(snd_pcm_poll_descriptors_count);
//_PA_LOAD_FUNC(snd_pcm_poll_descriptors_revents);
_PA_LOAD_FUNC(snd_pcm_format_size);
_PA_LOAD_FUNC(snd_pcm_link);
//_PA_LOAD_FUNC(snd_pcm_delay);

_PA_LOAD_FUNC(snd_pcm_channel_params);
//_PA_LOAD_FUNC(snd_pcm_hw_params_sizeof);
//_PA_LOAD_FUNC(snd_pcm_hw_params_malloc);
//_PA_LOAD_FUNC(snd_pcm_hw_params_free);
//_PA_LOAD_FUNC(snd_pcm_hw_params_any);
//_PA_LOAD_FUNC(snd_pcm_hw_params_set_access);
//_PA_LOAD_FUNC(snd_pcm_hw_params_set_format);
//_PA_LOAD_FUNC(snd_pcm_hw_params_set_channels);
////_PA_LOAD_FUNC(snd_pcm_hw_params_set_periods_near);
//_PA_LOAD_FUNC(snd_pcm_hw_params_set_rate_near);
//_PA_LOAD_FUNC(snd_pcm_hw_params_set_rate);
//_PA_LOAD_FUNC(snd_pcm_hw_params_set_rate_resample);
////_PA_LOAD_FUNC(snd_pcm_hw_params_set_buffer_time_near);
//_PA_LOAD_FUNC(snd_pcm_hw_params_set_buffer_size);
//_PA_LOAD_FUNC(snd_pcm_hw_params_set_buffer_size_near);
//_PA_LOAD_FUNC(snd_pcm_hw_params_set_buffer_size_min);
////_PA_LOAD_FUNC(snd_pcm_hw_params_set_period_time_near);
//_PA_LOAD_FUNC(snd_pcm_hw_params_set_period_size_near);
//_PA_LOAD_FUNC(snd_pcm_hw_params_set_periods_integer);
//_PA_LOAD_FUNC(snd_pcm_hw_params_set_periods_min);

_PA_LOAD_FUNC(snd_pcm_channel_setup);
////_PA_LOAD_FUNC(snd_pcm_hw_params_get_period_size);
////_PA_LOAD_FUNC(snd_pcm_hw_params_get_access);
////_PA_LOAD_FUNC(snd_pcm_hw_params_get_periods);
////_PA_LOAD_FUNC(snd_pcm_hw_params_get_rate);
//_PA_LOAD_FUNC(snd_pcm_hw_params_get_channels_min);
//_PA_LOAD_FUNC(snd_pcm_hw_params_get_channels_max);

//_PA_LOAD_FUNC(snd_pcm_channel_test_period_size);
//_PA_LOAD_FUNC(snd_pcm_channel_test_format);
//_PA_LOAD_FUNC(snd_pcm_channel_test_access);
//_PA_LOAD_FUNC(snd_pcm_hw_params_dump);
//_PA_LOAD_FUNC(snd_pcm_hw_params);

//_PA_LOAD_FUNC(snd_pcm_hw_params_get_periods_min);
//_PA_LOAD_FUNC(snd_pcm_hw_params_get_periods_max);
//_PA_LOAD_FUNC(snd_pcm_hw_params_set_period_size);
//_PA_LOAD_FUNC(snd_pcm_hw_params_get_period_size_min);
//_PA_LOAD_FUNC(snd_pcm_hw_params_get_period_size_max);
//_PA_LOAD_FUNC(snd_pcm_hw_params_get_buffer_size_max);
//_PA_LOAD_FUNC(snd_pcm_hw_params_get_rate_min);
//_PA_LOAD_FUNC(snd_pcm_hw_params_get_rate_max);
//_PA_LOAD_FUNC(snd_pcm_hw_params_get_rate_numden);

//_PA_LOAD_FUNC(snd_pcm_sw_params_sizeof);
//_PA_LOAD_FUNC(snd_pcm_sw_params_malloc);
//_PA_LOAD_FUNC(snd_pcm_sw_params_current);
//_PA_LOAD_FUNC(snd_pcm_sw_params_set_avail_min);
//_PA_LOAD_FUNC(snd_pcm_sw_params);
//_PA_LOAD_FUNC(snd_pcm_sw_params_free);
//_PA_LOAD_FUNC(snd_pcm_sw_params_set_start_threshold);
//_PA_LOAD_FUNC(snd_pcm_sw_params_set_stop_threshold);
//_PA_LOAD_FUNC(snd_pcm_sw_params_get_boundary);
//_PA_LOAD_FUNC(snd_pcm_sw_params_set_silence_threshold);
//_PA_LOAD_FUNC(snd_pcm_sw_params_set_silence_size);
//_PA_LOAD_FUNC(snd_pcm_sw_params_set_xfer_align);
//_PA_LOAD_FUNC(snd_pcm_sw_params_set_tstamp_mode);

//_PA_LOAD_FUNC(snd_pcm_hw_info);
_PA_LOAD_FUNC(snd_pcm_info);
_PA_LOAD_FUNC(snd_pcm_channel_info);
//_PA_LOAD_FUNC(snd_pcm_info_sizeof);
//_PA_LOAD_FUNC(snd_pcm_info_malloc);
//_PA_LOAD_FUNC(snd_pcm_info_free);
//_PA_LOAD_FUNC(snd_pcm_info_set_device);
//_PA_LOAD_FUNC(snd_pcm_info_set_subdevice);
//_PA_LOAD_FUNC(snd_pcm_info_set_stream);
//_PA_LOAD_FUNC(snd_pcm_info_get_name);
_PA_LOAD_FUNC(snd_card_name);
_PA_LOAD_FUNC(snd_cards_list);

//_PA_LOAD_FUNC(snd_ctl_pcm_next_device);
_PA_LOAD_FUNC(snd_ctl_pcm_info);
_PA_LOAD_FUNC(snd_ctl_open);
_PA_LOAD_FUNC(snd_ctl_hw_info);
//_PA_LOAD_FUNC(snd_ctl_close);
//_PA_LOAD_FUNC(snd_ctl_card_info_malloc);
//_PA_LOAD_FUNC(snd_ctl_card_info_free);
//_PA_LOAD_FUNC(snd_ctl_card_info);
//_PA_LOAD_FUNC(snd_ctl_card_info_sizeof);
//_PA_LOAD_FUNC(snd_ctl_card_info_get_name);
//
//_PA_LOAD_FUNC(snd_config);
//_PA_LOAD_FUNC(snd_config_update);
//_PA_LOAD_FUNC(snd_config_search);
//_PA_LOAD_FUNC(snd_config_iterator_entry);
//_PA_LOAD_FUNC(snd_config_iterator_first);
//_PA_LOAD_FUNC(snd_config_iterator_end);
//_PA_LOAD_FUNC(snd_config_iterator_next);
//_PA_LOAD_FUNC(snd_config_get_string);
//_PA_LOAD_FUNC(snd_config_get_id);
//_PA_LOAD_FUNC(snd_config_update_free_global);
//
//_PA_LOAD_FUNC(snd_pcm_status);
//_PA_LOAD_FUNC(snd_pcm_status_sizeof);
//_PA_LOAD_FUNC(snd_pcm_status_get_tstamp);
//_PA_LOAD_FUNC(snd_pcm_status_get_state);
//_PA_LOAD_FUNC(snd_pcm_status_get_trigger_tstamp);
//_PA_LOAD_FUNC(snd_pcm_status_get_delay);
//
//_PA_LOAD_FUNC(snd_card_next);
_PA_LOAD_FUNC(snd_strerror);
//_PA_LOAD_FUNC(snd_output_stdio_attach);
#undef _PA_LOAD_FUNC

#ifdef PA_IOAUDIO_DYNAMIC
	PA_DEBUG(( "%s: loaded IOAUDIO API - ok\n", __FUNCTION__ ));

#define _PA_VALIDATE_LOAD_REPLACEMENT(x)\
	do {\
		if (ioaudio_##x == NULL)\
		{\
			ioaudio_##x = &_PA_LOCAL_IMPL(x);\
			PA_DEBUG(( "%s: replacing [%s] with local implementation\n", __FUNCTION__, #x ));\
		}\
	} while (0)

#if 0
	_PA_VALIDATE_LOAD_REPLACEMENT(snd_pcm_hw_params_set_rate_near);
	_PA_VALIDATE_LOAD_REPLACEMENT(snd_pcm_hw_params_set_buffer_size_near);
	_PA_VALIDATE_LOAD_REPLACEMENT(snd_pcm_hw_params_set_period_size_near);
	_PA_VALIDATE_LOAD_REPLACEMENT(snd_pcm_hw_params_get_channels_min);
	_PA_VALIDATE_LOAD_REPLACEMENT(snd_pcm_hw_params_get_channels_max);
	_PA_VALIDATE_LOAD_REPLACEMENT(snd_pcm_hw_params_get_periods_min);
	_PA_VALIDATE_LOAD_REPLACEMENT(snd_pcm_hw_params_get_periods_max);
	_PA_VALIDATE_LOAD_REPLACEMENT(snd_pcm_hw_params_get_period_size_min);
	_PA_VALIDATE_LOAD_REPLACEMENT(snd_pcm_hw_params_get_period_size_max);
	_PA_VALIDATE_LOAD_REPLACEMENT(snd_pcm_hw_params_get_buffer_size_max);
	_PA_VALIDATE_LOAD_REPLACEMENT(snd_pcm_hw_params_get_rate_min);
	_PA_VALIDATE_LOAD_REPLACEMENT(snd_pcm_hw_params_get_rate_max);
#endif

#undef _PA_LOCAL_IMPL
#undef _PA_VALIDATE_LOAD_REPLACEMENT

#endif // PA_IOAUDIO_DYNAMIC

    return 1;
}

void PaIoaudio_SetLibraryPathName( const char *pathName )
{
#ifdef PA_IOAUDIO_DYNAMIC
    g_IoaudioLibName = pathName;
#else
    (void)pathName;
#endif
}

/* Close handle to ioaudio library. */
static void PaIoaudio_CloseLibrary()
{
#ifdef PA_IOAUDIO_DYNAMIC
    dlclose(g_IoaudioLib);
	g_IoaudioLib = NULL;
#endif
}

/* Check return value of IOAUDIO function, and map it to PaError */
#define ENSURE_(expr, code) \
    do { \
		int __pa_unsure_error_id;\
        if( UNLIKELY( (__pa_unsure_error_id = (expr)) < 0 ) ) \
        { \
            /* PaUtil_SetLastHostErrorInfo should only be used in the main thread */ \
            if( (code) == paUnanticipatedHostError && pthread_equal( pthread_self(), paUnixMainThread) ) \
            { \
                PaUtil_SetLastHostErrorInfo( paIoaudio, __pa_unsure_error_id, ioaudio_snd_strerror( __pa_unsure_error_id ) ); \
            } \
            PaUtil_DebugPrint( "Expression '" #expr "' failed in '" __FILE__ "', line: " STRINGIZE( __LINE__ ) "\n" ); \
            if( (code) == paUnanticipatedHostError ) \
                PA_DEBUG(( "Host error description: %s\n", ioaudio_snd_strerror( __pa_unsure_error_id ) )); \
            result = (code); \
            goto error; \
        } \
    } while (0)

#define ASSERT_CALL_(expr, success) \
    do {\
    	int __pa_assert_error_id;\
    	__pa_assert_error_id = (expr);\
		assert( success == __pa_assert_error_id );\
	} while (0)

static int numPeriods_ = 4;
static int busyRetries_ = 100;

int PaIoaudio_SetNumPeriods( int numPeriods )
{
    numPeriods_ = numPeriods;
    return paNoError;
}

typedef enum
{
    StreamDirection_In,
    StreamDirection_Out
} StreamDirection;

typedef struct
{
    PaSampleFormat hostSampleFormat;
    unsigned long framesPerBuffer;
    int numUserChannels, numHostChannels;
    int userInterleaved, hostInterleaved;
    int canMmap;
    void *nonMmapBuffer;
    unsigned int nonMmapBufferSize;
    PaDeviceIndex device;     /* Keep the device index */

    snd_pcm_t *pcm;
    snd_pcm_uframes_t bufferSize;
    snd_pcm_format_format_t nativeFormat;
    unsigned int nfds;
    int ready;  /* Marked ready from poll */
    void **userBuffers;
    snd_pcm_uframes_t offset;
    StreamDirection streamDir;

    snd_pcm_channel_area_t *channelAreas;  /* Needed for channel adaption */
} PaIoaudioStreamComponent;

/* Implementation specific stream structure */
typedef struct PaIoaudioStream
{
    PaUtilStreamRepresentation streamRepresentation;
    PaUtilCpuLoadMeasurer cpuLoadMeasurer;
    PaUtilBufferProcessor bufferProcessor;
    PaUnixThread thread;

    unsigned long framesPerUserBuffer, maxFramesPerHostBuffer;

    int primeBuffers;
    int callbackMode;              /* bool: are we running in callback mode? */
    int pcmsSynced;                /* Have we successfully synced pcms */
    int rtSched;

    /* the callback thread uses these to poll the sound device(s), waiting
     * for data to be ready/available */
    struct pollfd* pfds;
    int pollTimeout;

    /* Used in communication between threads */
    volatile sig_atomic_t callback_finished; /* bool: are we in the "callback finished" state? */
    volatile sig_atomic_t callbackAbort;    /* Drop frames? */
    volatile sig_atomic_t isActive;         /* Is stream in active state? (Between StartStream and StopStream || !paContinue) */
    PaUnixMutex stateMtx;                   /* Used to synchronize access to stream state */

    int neverDropInput;

    PaTime underrun;
    PaTime overrun;

    PaIoaudioStreamComponent capture, playback;
}
PaIoaudioStream;

/* PaIoaudioHostApiRepresentation - host api datastructure specific to this implementation */

typedef struct PaIoaudioHostApiRepresentation
{
    PaUtilHostApiRepresentation baseHostApiRep;
    PaUtilStreamInterface callbackStreamInterface;
    PaUtilStreamInterface blockingStreamInterface;

    PaUtilAllocationGroup *allocations;

    PaHostApiIndex hostApiIndex;
}
PaIoaudioHostApiRepresentation;

typedef struct PaIoaudioDeviceInfo
{
    PaDeviceInfo baseDeviceInfo;
    char *ioaudioName;
    int isPlug;
    int minInputChannels;
    int minOutputChannels;
}
PaIoaudioDeviceInfo;

/* prototypes for functions declared in this file */

static void Terminate( struct PaUtilHostApiRepresentation *hostApi );
static PaError IsFormatSupported( struct PaUtilHostApiRepresentation *hostApi,
                                  const PaStreamParameters *inputParameters,
                                  const PaStreamParameters *outputParameters,
                                  double sampleRate );
static PaError OpenStream( struct PaUtilHostApiRepresentation *hostApi,
                           PaStream** s,
                           const PaStreamParameters *inputParameters,
                           const PaStreamParameters *outputParameters,
                           double sampleRate,
                           unsigned long framesPerBuffer,
                           PaStreamFlags streamFlags,
                           PaStreamCallback *callback,
                           void *userData );
static PaError CloseStream( PaStream* stream );
static PaError StartStream( PaStream *stream );
static PaError StopStream( PaStream *stream );
static PaError AbortStream( PaStream *stream );
static PaError IsStreamStopped( PaStream *s );
static PaError IsStreamActive( PaStream *stream );
static PaTime GetStreamTime( PaStream *stream );
static double GetStreamCpuLoad( PaStream* stream );
static PaError BuildDeviceList( PaIoaudioHostApiRepresentation *hostApi );
//static int SetApproximateSampleRate( snd_pcm_t *pcm, snd_pcm_channel_params_t *hwParams, double sampleRate );
//static int GetExactSampleRate( snd_pcm_channel_params_t *hwParams, double *sampleRate );

/* Callback prototypes */
static void *CallbackThreadFunc( void *userData );

/* Blocking prototypes */
static signed long GetStreamReadAvailable( PaStream* s );
static signed long GetStreamWriteAvailable( PaStream* s );
static PaError ReadStream( PaStream* stream, void *buffer, unsigned long frames );
static PaError WriteStream( PaStream* stream, const void *buffer, unsigned long frames );

// non-existing functions defined for QNX
int ConvertStreamDirToChannelDir( StreamDirection streamDir )
{
    return (streamDir == StreamDirection_In ? SND_PCM_CHANNEL_CAPTURE : SND_PCM_CHANNEL_PLAYBACK );
}

void ioaudio_snd_pcm_hw_params_dump
    (
    snd_pcm_t * pcm
    )
{
    snd_pcm_channel_info_t params;
    int ret = ioaudio_snd_pcm_channel_info( pcm, &params );
    if( ret < 0 )
    {
        PA_DEBUG(( "snd_pcm_channel_info: %s\n", snd_strerror( ret ) ));
    }
    else
    {
        PA_DEBUG(("===snd_pcm_channel_info====\n"));
        PA_DEBUG(("subdevice       =%d\n", params.subdevice));
        PA_DEBUG(("subname         =%s\n", params.subname));
        PA_DEBUG(("channel         =%d\n", params.channel));
        PA_DEBUG(("flags           =%x\n", params.flags  ));
        PA_DEBUG(("formats         =%d\n", params.formats));
        PA_DEBUG(("rates           =%x\n", params.rates));
        PA_DEBUG(("min_rate        =%d\n", params.min_rate));
        PA_DEBUG(("max_rate        =%d\n", params.max_rate));
        PA_DEBUG(("min_voices      =%d\n", params.min_voices));
        PA_DEBUG(("max_voices      =%d\n", params.max_voices));
        PA_DEBUG(("max_buffer_size =%d\n", params.max_buffer_size));
        PA_DEBUG(("frags_min       =%d\n", params.min_fragment_size));
        PA_DEBUG(("frags_max       =%d\n", params.max_fragment_size));
        PA_DEBUG(("frag_align      =%d\n", params.fragment_align));
        PA_DEBUG(("fifo_size       =%d\n", params.fifo_size));
        PA_DEBUG(("block_size      =%d\n", params.transfer_block_size));
    }
}

PaError ioaudio_snd_pcm_delay( snd_pcm_t* pcm, snd_pcm_sframes_t* delayp )
{
    PaError result = paNoError;
    *delayp = 0; // this should be driver and DMA delay
    return result;
}
// end non-existing functions defined for QNX

static const PaIoaudioDeviceInfo *GetDeviceInfo( const PaUtilHostApiRepresentation *hostApi, int device )
{
    return (const PaIoaudioDeviceInfo *)hostApi->deviceInfos[device];
}

/** Uncommented because AlsaErrorHandler is unused for anything good yet. If AlsaErrorHandler is
    to be used, do not forget to register this callback in PaIoaudio_Initialize, and unregister in Terminate.
*/
/*static void AlsaErrorHandler(const char *file, int line, const char *function, int err, const char *fmt, ...)
{
}*/

PaError PaIoaudio_Initialize( PaUtilHostApiRepresentation **hostApi, PaHostApiIndex hostApiIndex )
{
    PaError result = paNoError;
    PaIoaudioHostApiRepresentation *ioaudioHostApi = NULL;

    /* Try loading ioaudio library. */
    if (!PaIoaudio_LoadLibrary())
        return paHostApiNotFound;

    PA_UNLESS( ioaudioHostApi = (PaIoaudioHostApiRepresentation*) PaUtil_AllocateMemory(
                sizeof(PaIoaudioHostApiRepresentation) ), paInsufficientMemory );
    PA_UNLESS( ioaudioHostApi->allocations = PaUtil_CreateAllocationGroup(), paInsufficientMemory );
    ioaudioHostApi->hostApiIndex = hostApiIndex;

    *hostApi = (PaUtilHostApiRepresentation*)ioaudioHostApi;
    (*hostApi)->info.structVersion = 1;
    (*hostApi)->info.type = paIoaudio;
    (*hostApi)->info.name = "IOAUDIO";

    (*hostApi)->Terminate = Terminate;
    (*hostApi)->OpenStream = OpenStream;
    (*hostApi)->IsFormatSupported = IsFormatSupported;

    /** If AlsaErrorHandler is to be used, do not forget to unregister callback pointer in
        Terminate function.
    */
    /*ENSURE_( snd_lib_error_set_handler(AlsaErrorHandler), paUnanticipatedHostError );*/

    PA_ENSURE( BuildDeviceList( ioaudioHostApi ) );

    PaUtil_InitializeStreamInterface( &ioaudioHostApi->callbackStreamInterface,
                                      CloseStream, StartStream,
                                      StopStream, AbortStream,
                                      IsStreamStopped, IsStreamActive,
                                      GetStreamTime, GetStreamCpuLoad,
                                      PaUtil_DummyRead, PaUtil_DummyWrite,
                                      PaUtil_DummyGetReadAvailable,
                                      PaUtil_DummyGetWriteAvailable );

    PaUtil_InitializeStreamInterface( &ioaudioHostApi->blockingStreamInterface,
                                      CloseStream, StartStream,
                                      StopStream, AbortStream,
                                      IsStreamStopped, IsStreamActive,
                                      GetStreamTime, PaUtil_DummyGetCpuLoad,
                                      ReadStream, WriteStream,
                                      GetStreamReadAvailable,
                                      GetStreamWriteAvailable );

    PA_ENSURE( PaUnixThreading_Initialize() );

    return result;

error:
    if( ioaudioHostApi )
    {
        if( ioaudioHostApi->allocations )
        {
            PaUtil_FreeAllAllocations( ioaudioHostApi->allocations );
            PaUtil_DestroyAllocationGroup( ioaudioHostApi->allocations );
        }

        PaUtil_FreeMemory( ioaudioHostApi );
    }

    return result;
}

static void Terminate( struct PaUtilHostApiRepresentation *hostApi )
{
    PaIoaudioHostApiRepresentation *ioaudioHostApi = (PaIoaudioHostApiRepresentation*)hostApi;

    assert( hostApi );

    /** See AlsaErrorHandler and PaIoaudio_Initialize for details.
    */
    /*snd_lib_error_set_handler(NULL);*/

    if( ioaudioHostApi->allocations )
    {
        PaUtil_FreeAllAllocations( ioaudioHostApi->allocations );
        PaUtil_DestroyAllocationGroup( ioaudioHostApi->allocations );
    }

    PaUtil_FreeMemory( ioaudioHostApi );
    //ioaudio_snd_config_update_free_global();

    /* Close Alsa library. */
    PaIoaudio_CloseLibrary();
}

/** Determine max channels and default latencies.
 *
 * This function provides functionality to grope an opened (might be opened for capture or playback) pcm device for
 * traits like max channels, suitable default latencies and default sample rate. Upon error, max channels is set to zero,
 * and a suitable result returned. The device is closed before returning.
 */
static PaError GropeDevice( snd_pcm_t* pcm, int isPlug, StreamDirection mode, int openBlocking,
        PaIoaudioDeviceInfo* devInfo )
{
#if 1
    snd_pcm_channel_info_t chInfo;
#endif
    PaError result = paNoError;
    //snd_pcm_channel_params_t *hwParams;
    snd_pcm_uframes_t lowLatency = 512, highLatency = 2048;
    unsigned int minChans, maxChans;
    int* minChannels, * maxChannels;
    double * defaultLowLatency, * defaultHighLatency, * defaultSampleRate =
        &devInfo->baseDeviceInfo.defaultSampleRate;
    double defaultSr = *defaultSampleRate;

    assert( pcm );

    PA_DEBUG(( "%s: collecting info ..\n", __FUNCTION__ ));

    if( StreamDirection_In == mode )
    {
        minChannels = &devInfo->minInputChannels;
        maxChannels = &devInfo->baseDeviceInfo.maxInputChannels;
        defaultLowLatency = &devInfo->baseDeviceInfo.defaultLowInputLatency;
        defaultHighLatency = &devInfo->baseDeviceInfo.defaultHighInputLatency;
    }
    else
    {
        minChannels = &devInfo->minOutputChannels;
        maxChannels = &devInfo->baseDeviceInfo.maxOutputChannels;
        defaultLowLatency = &devInfo->baseDeviceInfo.defaultLowOutputLatency;
        defaultHighLatency = &devInfo->baseDeviceInfo.defaultHighOutputLatency;
    }

    ENSURE_( ioaudio_snd_pcm_nonblock_mode( pcm, 0 ), paUnanticipatedHostError );

#if 1
    result = ioaudio_snd_pcm_channel_info( pcm, &chInfo );
    if( paNoError != result )
    {
        result = paUnanticipatedHostError;
        goto error;
    }

    minChans = chInfo.min_voices;
    maxChans = chInfo.max_voices;
    lowLatency = chInfo.min_fragment_size;
    highLatency = chInfo.max_fragment_size;
#else
    ioaudio_snd_pcm_hw_params_alloca( &hwParams );
    //ioaudio_snd_pcm_hw_params_any( pcm, hwParams );

    if( defaultSr >= 0 )
    {
        /* Could be that the device opened in one mode supports samplerates that the other mode wont have,
         * so try again .. */
        if( SetApproximateSampleRate( pcm, hwParams, defaultSr ) < 0 )
        {
            defaultSr = -1.;
            PA_DEBUG(( "%s: Original default samplerate failed, trying again ..\n", __FUNCTION__ ));
        }
    }

    if( defaultSr < 0. )           /* Default sample rate not set */
    {
        unsigned int sampleRate = 44100;        /* Will contain approximate rate returned by alsa-lib */
        if( ioaudio_snd_pcm_hw_params_set_rate_near( pcm, hwParams, &sampleRate, NULL ) < 0)
        {
            result = paUnanticipatedHostError;
            goto error;
        }
        ENSURE_( GetExactSampleRate( hwParams, &defaultSr ), paUnanticipatedHostError );
    }

    ENSURE_( ioaudio_snd_pcm_hw_params_get_channels_min( hwParams, &minChans ), paUnanticipatedHostError );
    ENSURE_( ioaudio_snd_pcm_hw_params_get_channels_max( hwParams, &maxChans ), paUnanticipatedHostError );
    assert( maxChans <= INT_MAX );
    assert( maxChans > 0 );    /* Weird linking issue could cause wrong version of IOAUDIO symbols to be called,
                                   resulting in zeroed values */

    /* XXX: Limit to sensible number (IOAUDIO plugins accept a crazy amount of channels)? */
    if( isPlug && maxChans > 128 )
    {
        maxChans = 128;
        PA_DEBUG(( "%s: Limiting number of plugin channels to %u\n", __FUNCTION__, maxChans ));
    }

    /* TWEAKME:
     *
     * Giving values for default min and max latency is not
     * straightforward.  Here are our objectives:
     *
     *         * for low latency, we want to give the lowest value
     *         that will work reliably.  This varies based on the
     *         sound card, kernel, CPU, etc.  I think it is better
     *         to give sub-optimal latency than to give a number
     *         too low and cause dropouts.  My conservative
     *         estimate at this point is to base it on 4096-sample
     *         latency at 44.1 kHz, which gives a latency of 23ms.
     *         * for high latency we want to give a large enough
     *         value that dropouts are basically impossible.  This
     *         doesn't really require as much tweaking, since
     *         providing too large a number will just cause us to
     *         select the nearest setting that will work at stream
     *         config time.
     */
    ENSURE_( ioaudio_snd_pcm_hw_params_set_buffer_size_near( pcm, hwParams, &lowLatency ), paUnanticipatedHostError );

    /* Have to reset hwParams, to set new buffer size */
    ENSURE_( ioaudio_snd_pcm_hw_params_any( pcm, hwParams ), paUnanticipatedHostError );
    ENSURE_( ioaudio_snd_pcm_hw_params_set_buffer_size_near( pcm, hwParams, &highLatency ), paUnanticipatedHostError );
#endif

    *minChannels = (int)minChans;
    *maxChannels = (int)maxChans;
    *defaultSampleRate = defaultSr;
    *defaultLowLatency = (double) lowLatency / *defaultSampleRate;
    *defaultHighLatency = (double) highLatency / *defaultSampleRate;

end:
    ioaudio_snd_pcm_close( pcm );
    return result;

error:
    goto end;
}

/* Initialize device info with invalid values (maxInputChannels and maxOutputChannels are set to zero since these indicate
 * wether input/output is available) */
static void InitializeDeviceInfo( PaDeviceInfo *deviceInfo )
{
    deviceInfo->structVersion = -1;
    deviceInfo->name = NULL;
    deviceInfo->hostApi = -1;
    deviceInfo->maxInputChannels = 0;
    deviceInfo->maxOutputChannels = 0;
    deviceInfo->defaultLowInputLatency = -1.;
    deviceInfo->defaultLowOutputLatency = -1.;
    deviceInfo->defaultHighInputLatency = -1.;
    deviceInfo->defaultHighOutputLatency = -1.;
    deviceInfo->defaultSampleRate = -1.;
}

/* Helper struct */
typedef struct
{
    char *ioaudioName;
    char *name;
    int isPlug;
    int hasPlayback;
    int hasCapture;
} HwDevInfo;


HwDevInfo predefinedNames[] = {
    { "bt_in", NULL, 0, 0, 1 },
    { "bt_out", NULL, 0, 1, 0 },
    { "chime", NULL, 0, 1, 0 },
    { "media1", NULL, 0, 1, 0 },
    { "media2", NULL, 0, 1, 0 },
    { "mic", NULL, 0, 0, 1 },
    { "phone", NULL, 0, 1, 0 },
    { "voice", NULL, 0, 1, 0 },
    { "center_lfe", NULL, 0, 1, 0 },
/* { "default", NULL, 0, 1, 0 }, */
/* { "dmix", NULL, 0, 1, 0 }, */
/* { "dpl", NULL, 0, 1, 0 }, */
/* { "dsnoop", NULL, 0, 1, 0 }, */
    { "front", NULL, 0, 1, 0 },
    { "iec958", NULL, 0, 1, 0 },
/* { "modem", NULL, 0, 1, 0 }, */
    { "rear", NULL, 0, 1, 0 },
    { "side", NULL, 0, 1, 0 },
/*     { "spdif", NULL, 0, 0, 0 }, */
    { "surround40", NULL, 0, 1, 0 },
    { "surround41", NULL, 0, 1, 0 },
    { "surround50", NULL, 0, 1, 0 },
    { "surround51", NULL, 0, 1, 0 },
    { "surround71", NULL, 0, 1, 0 },

    { NULL, NULL, 0, 1, 0 }
};

#if 0
static const HwDevInfo *FindDeviceName( const char *name )
{
    int i;

    for( i = 0; predefinedNames[i].ioaudioName; i++ )
    {
        if( strcmp( name, predefinedNames[i].ioaudioName ) == 0 )
        {
            return &predefinedNames[i];
        }
    }

    return NULL;
}
#endif

#if 0
static PaError PaIoaudio_StrDup( PaIoaudioHostApiRepresentation *alsaApi,
        char **dst,
        const char *src)
{
    PaError result = paNoError;
    int len = strlen( src ) + 1;

    /* PA_DEBUG(("PaStrDup %s %d\n", src, len)); */

    PA_UNLESS( *dst = (char *)PaUtil_GroupAllocateMemory( alsaApi->allocations, len ),
            paInsufficientMemory );
    strncpy( *dst, src, len );

error:
    return result;
}
#endif

#if 0
/* Disregard some standard plugins
 */
static int IgnorePlugin( const char *pluginId )
{
    static const char *ignoredPlugins[] = {"hw", "plughw", "plug", "dsnoop", "tee",
        "file", "null", "shm", "cards", "rate_convert", NULL};
    int i = 0;
    while( ignoredPlugins[i] )
    {
        if( !strcmp( pluginId, ignoredPlugins[i] ) )
        {
            return 1;
        }
        ++i;
    }

    return 0;
}
#endif

static PaError IoaudioName2Numbers(const char *name, int *card, int *device)
{
    PaError ret = paNoError;
    char *hwPtr;
    char *commaPtr;
    int cardNumPosition, devNumPosition;
    char numString[32];
    int cardNumLen, devNumLen;
    *card = 0;
    *device = 0;
    hwPtr = strstr(name, "hw:");
    if(!hwPtr)
    {
        ret = paInvalidDevice;
    }
    else
    {
        cardNumPosition = hwPtr + 3 - name;
        commaPtr = strrchr(name, ',');
        if(!commaPtr)
        {
            ret = paInvalidDevice;
        }
        else
        {
            cardNumLen = (int)( (commaPtr - 1) - cardNumPosition );
            devNumPosition = commaPtr + 1 - name;
            devNumLen = strlen(name) - devNumPosition + 1;

            strncpy(numString, name+cardNumPosition, cardNumLen);
            numString[31] = '\0';
            *card = atoi(numString);
            strncpy(numString, name+devNumPosition, devNumLen);
            numString[31] = '\0';
            *device = atoi(numString);
        }
    }
    return ret;
}

/** Open PCM device.
 *
 * Wrapper around ioaudio_snd_pcm_open which may repeatedly retry opening a device if it is busy, for
 * a certain time. This is because dmix may temporarily hold on to a device after it (dmix)
 * has been opened and closed.
 * @param mode: Open mode (e.g., SND_PCM_BLOCKING).
 * @param waitOnBusy: Retry opening busy device for up to one second?
 **/
static int OpenPcm( snd_pcm_t **pcmp, const char *name, int stream, int mode, int waitOnBusy )
{
    bool openByName = false;
    int tries = 0, maxTries = waitOnBusy ? busyRetries_ : 0;
    int cardNum, devNum;
    int ret = IoaudioName2Numbers(name, &cardNum, &devNum);
    if(paNoError != ret)
        openByName = true;

    if(openByName)
        ret = ioaudio_snd_pcm_open_name( pcmp, name, ( stream | mode ) );
    else
        ret = ioaudio_snd_pcm_open( pcmp, cardNum, devNum, ( stream | mode ) );
    for( tries = 0; tries < maxTries && -EBUSY == ret; ++tries )
    {
        Pa_Sleep( 10 );
        if(openByName)
            ret = ioaudio_snd_pcm_open_name( pcmp, name, ( stream | mode ) );
        else
            ret = ioaudio_snd_pcm_open( pcmp, cardNum, devNum, ( stream | mode ) );
        if( -EBUSY != ret )
        {
            PA_DEBUG(( "%s: Successfully opened initially busy device after %d tries\n",
                        __FUNCTION__, tries ));
        }
    }
    if( -EBUSY == ret )
    {
        PA_DEBUG(( "%s: Failed to open busy device '%s'\n", __FUNCTION__, name ));
    }
    else
    {
    	if (ret < 0)
			PA_DEBUG(( "%s: Opened device '%s' ptr[%p] - result: [%d:%s]\n", __FUNCTION__, name, *pcmp, ret, ioaudio_snd_strerror(ret) ));
    }

    return ret;
}

static PaError FillInDevInfo( PaIoaudioHostApiRepresentation *ioaudioApi, HwDevInfo* deviceName, int blocking,
        PaIoaudioDeviceInfo* devInfo, int* devIdx )
{
    PaError result = 0;
    PaDeviceInfo *baseDeviceInfo = &devInfo->baseDeviceInfo;
    snd_pcm_t *pcm = NULL;
    PaUtilHostApiRepresentation *baseApi = &ioaudioApi->baseHostApiRep;
    blocking = (blocking) ? 0: SND_PCM_OPEN_NONBLOCK;

	PA_DEBUG(( "%s: filling device info for: %s\n", __FUNCTION__, deviceName->name ));

    /* Zero fields */
    InitializeDeviceInfo( baseDeviceInfo );

    /* to determine device capabilities, we must open the device and query the
     * hardware parameter configuration space */

    /* Query capture */
    if( deviceName->hasCapture &&
            OpenPcm( &pcm, deviceName->ioaudioName, SND_PCM_OPEN_CAPTURE, blocking, 0 )
            >= 0 )
    {
        if( GropeDevice( pcm, deviceName->isPlug, StreamDirection_In, blocking, devInfo ) != paNoError )
        {
            /* Error */
            PA_DEBUG(("%s: Failed groping %s for capture\n", __FUNCTION__, deviceName->ioaudioName));
            goto end;
        }
    }

    /* Query playback */
    if( deviceName->hasPlayback &&
            OpenPcm( &pcm, deviceName->ioaudioName, SND_PCM_OPEN_PLAYBACK, blocking, 0 )
            >= 0 )
    {
        if( GropeDevice( pcm, deviceName->isPlug, StreamDirection_Out, blocking, devInfo ) != paNoError )
        {
            /* Error */
            PA_DEBUG(("%s: Failed groping %s for playback\n", __FUNCTION__, deviceName->ioaudioName));
            goto end;
        }
    }

    baseDeviceInfo->structVersion = 2;
    baseDeviceInfo->hostApi = ioaudioApi->hostApiIndex;
    baseDeviceInfo->name = deviceName->name;
    devInfo->ioaudioName = deviceName->ioaudioName;
    devInfo->isPlug = deviceName->isPlug;

    /* A: Storing pointer to PaIoaudioDeviceInfo object as pointer to PaDeviceInfo object.
     * Should now be safe to add device info, unless the device supports neither capture nor playback
     */
    if( baseDeviceInfo->maxInputChannels > 0 || baseDeviceInfo->maxOutputChannels > 0 )
    {
        /* Make device default if there isn't already one or it is the IOAUDIO "default" device */
        if( (baseApi->info.defaultInputDevice == paNoDevice || !strcmp(deviceName->ioaudioName,
                        "default" )) && baseDeviceInfo->maxInputChannels > 0 )
        {
            baseApi->info.defaultInputDevice = *devIdx;
            PA_DEBUG(("Default input device: %s\n", deviceName->name));
        }
        if( (baseApi->info.defaultOutputDevice == paNoDevice || !strcmp(deviceName->ioaudioName,
                        "default" )) && baseDeviceInfo->maxOutputChannels > 0 )
        {
            baseApi->info.defaultOutputDevice = *devIdx;
            PA_DEBUG(("Default output device: %s\n", deviceName->name));
        }
        PA_DEBUG(("%s: Adding device %s: %d\n", __FUNCTION__, deviceName->name, *devIdx));
        baseApi->deviceInfos[*devIdx] = (PaDeviceInfo *) devInfo;
        (*devIdx) += 1;
    }
    else
    {
    	PA_DEBUG(( "%s: skipped device: %s, all channels - 0\n", __FUNCTION__, deviceName->name ));
    }

end:
    return result;
}

/* Build PaDeviceInfo list, ignore devices for which we cannot determine capabilities (possibly busy, sigh) */
#define NUM_CARDS 32
static PaError BuildDeviceList( PaIoaudioHostApiRepresentation *ioaudioApi )
{
    // io-audio flow  hw:0,0 (cardidx, devidx)
    // for each card by cardindex
    //   open ctl
    //   get card info to get card name
    //   for each device in the ctl
    //     get device pcminfo
    //     check capture/playback
    //     get device name
    //     count up num devices
    //     set hwDevInfos
    //   close ctl
    // check plugin devices and add plugin devices in the list
    PaUtilHostApiRepresentation *baseApi = &ioaudioApi->baseHostApiRep;
    PaIoaudioDeviceInfo *deviceInfoArray;
    int i, j, numCards, len;
    int devIdx = 0;
    int cardIdx[NUM_CARDS];
    int cardsOver;

    PaError result = paNoError;
    size_t numDeviceNames = 0;
    HwDevInfo *hwDevInfos = NULL;
//    snd_config_t *topNode = NULL;
    int blocking = SND_PCM_OPEN_NONBLOCK;
#ifdef PA_ENABLE_DEBUG_OUTPUT
    PaTime startTime = PaUtil_GetTime();
#endif

    if( getenv( "PA_IOAUDIO_INITIALIZE_BLOCK" ) && atoi( getenv( "PA_IOAUDIO_INITIALIZE_BLOCK" ) ) )
        blocking = 0;

    /* These two will be set to the first working input and output device, respectively */
    baseApi->info.defaultInputDevice = paNoDevice;
    baseApi->info.defaultOutputDevice = paNoDevice;

    // iterate over ctl devices
    // iterate over plugin devices

    // sound card info -> for each sound card, use snd_ctl_hw_info to get device info
    numCards = ioaudio_snd_cards_list( cardIdx, NUM_CARDS, &cardsOver );
    for( i = 0; i < numCards; i++ )
    {
        snd_ctl_t *ctl;
        snd_ctl_hw_info_t cardInfo;
        if( ioaudio_snd_ctl_open( &ctl, cardIdx[ i ] ) < 0 )
        {
            /* Unable to open card :( */
            PA_DEBUG(( "%s: Unable to open device id %d\n", __FUNCTION__, cardIdx[ i ] ));
            continue;
        }
        ioaudio_snd_ctl_hw_info( ctl, &cardInfo );
        // only check pcmdevices for now
        for(j = 0; j < cardInfo.pcmdevs; j++)
        {
            char *ioaudioName;
            snd_pcm_info_t pcmInfo;
            ++numDeviceNames;
            ioaudio_snd_ctl_pcm_info(ctl, j, &pcmInfo);
            len = snprintf(NULL, 0, "hw:%d,%d", cardIdx[i], j) + 1; // determine string length
            PA_UNLESS( ioaudioName = (char *)PaUtil_GroupAllocateMemory( ioaudioApi->allocations, len ), paInsufficientMemory );
            snprintf(ioaudioName, len, "%d:%d", cardIdx[i], j);
            hwDevInfos[ i ].ioaudioName = ioaudioName;
            hwDevInfos[ i ].name = pcmInfo.name;
            hwDevInfos[ i ].isPlug = 0;
            hwDevInfos[ i ].hasPlayback = (pcmInfo.playback > 0);
            hwDevInfos[ i ].hasCapture = (pcmInfo.capture > 0);
        }

    }
    // search config tree by snd_config_search (pcm node, type, for each, get id, and device name

    /* allocate deviceInfo memory based on the number of devices */
    PA_UNLESS( baseApi->deviceInfos = (PaDeviceInfo**)PaUtil_GroupAllocateMemory(
            ioaudioApi->allocations, sizeof(PaDeviceInfo*) * (numDeviceNames) ), paInsufficientMemory );

    /* allocate all device info structs in a contiguous block */
    PA_UNLESS( deviceInfoArray = (PaIoaudioDeviceInfo*)PaUtil_GroupAllocateMemory(
            ioaudioApi->allocations, sizeof(PaIoaudioDeviceInfo) * numDeviceNames ), paInsufficientMemory );

    /* Loop over list of cards, filling in info. If a device is deemed unavailable (can't get name),
     * it's ignored.
     *
     * Note that we do this in two stages. This is a workaround owing to the fact that the 'dmix'
     * plugin may cause the underlying hardware device to be busy for a short while even after it
     * (dmix) is closed. The 'default' plugin may also point to the dmix plugin, so the same goes
     * for this.
     */
    PA_DEBUG(( "%s: filling device info for %d devices\n", __FUNCTION__, numDeviceNames ));
    for( i = 0, devIdx = 0; i < numDeviceNames; ++i )
    {
        PaIoaudioDeviceInfo* devInfo = &deviceInfoArray[i];
        HwDevInfo* hwInfo = &hwDevInfos[i];
        PA_ENSURE( FillInDevInfo( ioaudioApi, hwInfo, blocking, devInfo, &devIdx ) );
    }
    assert( devIdx < numDeviceNames );
    free( hwDevInfos );

    baseApi->info.deviceCount = devIdx;   /* Number of successfully queried devices */

#ifdef PA_ENABLE_DEBUG_OUTPUT
    PA_DEBUG(( "%s: Building device list took %f seconds\n", __FUNCTION__, PaUtil_GetTime() - startTime ));
#endif

end:
    return result;

error:
    /* No particular action */
    goto end;
}

/* Check against known device capabilities */
static PaError ValidateParameters( const PaStreamParameters *parameters, PaUtilHostApiRepresentation *hostApi, StreamDirection mode )
{
    PaError result = paNoError;
    int maxChans;
    const PaIoaudioDeviceInfo *deviceInfo = NULL;
    assert( parameters );

    if( parameters->device != paUseHostApiSpecificDeviceSpecification )
    {
        assert( parameters->device < hostApi->info.deviceCount );
        PA_UNLESS( parameters->hostApiSpecificStreamInfo == NULL, paBadIODeviceCombination );
        deviceInfo = GetDeviceInfo( hostApi, parameters->device );
    }
    else
    {
        const PaIoaudioStreamInfo *streamInfo = parameters->hostApiSpecificStreamInfo;

        PA_UNLESS( parameters->device == paUseHostApiSpecificDeviceSpecification, paInvalidDevice );
        PA_UNLESS( streamInfo->size == sizeof (PaIoaudioStreamInfo) && streamInfo->version == 1,
                paIncompatibleHostApiSpecificStreamInfo );
        PA_UNLESS( streamInfo->deviceString != NULL, paInvalidDevice );

        /* Skip further checking */
        return paNoError;
    }

    assert( deviceInfo );
    assert( parameters->hostApiSpecificStreamInfo == NULL );
    maxChans = (StreamDirection_In == mode ? deviceInfo->baseDeviceInfo.maxInputChannels :
        deviceInfo->baseDeviceInfo.maxOutputChannels);
    PA_UNLESS( parameters->channelCount <= maxChans, paInvalidChannelCount );

error:
    return result;
}

/* Given an open stream, what sample formats are available? */
static PaSampleFormat GetAvailableFormats( snd_pcm_t *pcm )
{
    PaSampleFormat available = 0;
    available |= paFloat32;
    available |= paInt32;
    available |= paInt24;
    available |= paInt16;
    available |= paUInt8;
    available |= paInt8;

    return available;
}

/* Output to console all formats supported by device */
static void LogAllAvailableFormats( snd_pcm_t *pcm )
{
    int i;
	PA_DEBUG(( " --- Supported Formats ---\n" ));

    for(i = 0; i < SND_PCM_SFMT_SPECIAL; i++)
    {
        PA_DEBUG(( "%d:%s\n", i, snd_pcm_get_format_name(i) ));
    }
    PA_DEBUG(( " -------------------------\n" ));
}

static snd_pcm_format_format_t Pa2IoaudioFormat( PaSampleFormat paFormat )
{
    switch( paFormat )
    {
        case paFloat32:
            return SND_PCM_SFMT_FLOAT_LE;

        case paInt16:
            return SND_PCM_SFMT_S16_LE;

        case paInt24:
            return SND_PCM_SFMT_S24_LE;

        case paInt32:
            return SND_PCM_SFMT_S32_LE;

        case paInt8:
            return SND_PCM_SFMT_S8;

        case paUInt8:
            return SND_PCM_SFMT_U8;

        default:
            return SND_PCM_SFMT_S32_LE;
    }
}

/** Open an IOAUDIO pcm handle.
 *
 * The device to be open can be specified in a custom PaIoaudioStreamInfo struct, or it will be a device number. In case of a
 * device number, it maybe specified through an env variable (PA_IOAUDIO_PLUGHW) that we should open the corresponding plugin
 * device.
 */
static PaError IoaudioOpen( const PaUtilHostApiRepresentation *hostApi, const PaStreamParameters *params, StreamDirection
        streamDir, snd_pcm_t **pcm )
{
    PaError result = paNoError;
    int ret;
    char dnameArray[50];
    const char* deviceName = dnameArray;
    const PaIoaudioDeviceInfo *deviceInfo = NULL;
    PaIoaudioStreamInfo *streamInfo = (PaIoaudioStreamInfo *)params->hostApiSpecificStreamInfo;

    if( !streamInfo )
    {
        int usePlug = 0;
        deviceInfo = GetDeviceInfo( hostApi, params->device );

        /* If device name starts with hw: and PA_IOAUDIO_PLUGHW is 1, we open the plughw device instead */
        if( !strncmp( "hw:", deviceInfo->ioaudioName, 3 ) && getenv( "PA_IOAUDIO_PLUGHW" ) )
            usePlug = atoi( getenv( "PA_IOAUDIO_PLUGHW" ) );
        if( usePlug )
            snprintf( dnameArray, 50, "plug%s", deviceInfo->ioaudioName );
        else
            deviceName = deviceInfo->ioaudioName;
    }
    else
        deviceName = streamInfo->deviceString;

    PA_DEBUG(( "%s: Opening device %s\n", __FUNCTION__, deviceName ));
    if( (ret = OpenPcm( pcm, deviceName, streamDir == StreamDirection_In ? SND_PCM_OPEN_CAPTURE : SND_PCM_OPEN_PLAYBACK,
                    SND_PCM_OPEN_NONBLOCK, 1 )) < 0 )
    {
        /* Not to be closed */
        *pcm = NULL;
        ENSURE_( ret, -EBUSY == ret ? paDeviceUnavailable : paBadIODeviceCombination );
    }

end:
    return result;

error:
    goto end;
}

#if 0
static PaError TestParameters( const PaUtilHostApiRepresentation *hostApi, const PaStreamParameters *parameters,
        double sampleRate, StreamDirection streamDir )
{
    PaError result = paNoError;
    snd_pcm_t *pcm = NULL;
    PaSampleFormat availableFormats;
    /* We are able to adapt to a number of channels less than what the device supports */
    unsigned int numHostChannels;
    PaSampleFormat hostFormat;
    snd_pcm_channel_params_t *hwParams;
    ioaudio_snd_pcm_hw_params_alloca( &hwParams );

    if( !parameters->hostApiSpecificStreamInfo )
    {
        const PaIoaudioDeviceInfo *devInfo = GetDeviceInfo( hostApi, parameters->device );
        numHostChannels = PA_MAX( parameters->channelCount, StreamDirection_In == streamDir ?
                devInfo->minInputChannels : devInfo->minOutputChannels );
    }
    else
        numHostChannels = parameters->channelCount;

    PA_ENSURE( IoaudioOpen( hostApi, parameters, streamDir, &pcm ) );

    ioaudio_snd_pcm_hw_params_any( pcm, hwParams );

    if( SetApproximateSampleRate( pcm, hwParams, sampleRate ) < 0 )
    {
        result = paInvalidSampleRate;
        goto error;
    }

    if( ioaudio_snd_pcm_hw_params_set_channels( pcm, hwParams, numHostChannels ) < 0 )
    {
        result = paInvalidChannelCount;
        goto error;
    }

    /* See if we can find a best possible match */
    availableFormats = GetAvailableFormats( pcm );
    PA_ENSURE( hostFormat = PaUtil_SelectClosestAvailableFormat( availableFormats, parameters->sampleFormat ) );

    /* Some specific hardware (reported: Audio8 DJ) can fail with assertion during this step. */
    ENSURE_( ioaudio_snd_pcm_hw_params_set_format( pcm, hwParams, Pa2IoaudioFormat( hostFormat ) ), paUnanticipatedHostError );

    {
        /* It happens that this call fails because the device is busy */
        int ret = 0;
        if( (ret = ioaudio_snd_pcm_hw_params( pcm, hwParams )) < 0)
        {
            if( -EINVAL == ret )
            {
                /* Don't know what to return here */
                result = paBadIODeviceCombination;
                goto error;
            }
            else if( -EBUSY == ret )
            {
                result = paDeviceUnavailable;
                PA_DEBUG(( "%s: Device is busy\n", __FUNCTION__ ));
            }
            else
            {
                result = paUnanticipatedHostError;
            }

            ENSURE_( ret, result );
        }
    }

end:
    if( pcm )
    {
        ioaudio_snd_pcm_close( pcm );
    }
    return result;

error:
    goto end;
}
#endif

static PaError IsFormatSupported( struct PaUtilHostApiRepresentation *hostApi,
                                  const PaStreamParameters *inputParameters,
                                  const PaStreamParameters *outputParameters,
                                  double sampleRate )
{
#if 0
    int inputChannelCount = 0, outputChannelCount = 0;
    PaSampleFormat inputSampleFormat, outputSampleFormat;
    PaError result = paFormatIsSupported;

    if( inputParameters )
    {
        PA_ENSURE( ValidateParameters( inputParameters, hostApi, StreamDirection_In ) );

        inputChannelCount = inputParameters->channelCount;
        inputSampleFormat = inputParameters->sampleFormat;
    }

    if( outputParameters )
    {
        PA_ENSURE( ValidateParameters( outputParameters, hostApi, StreamDirection_Out ) );

        outputChannelCount = outputParameters->channelCount;
        outputSampleFormat = outputParameters->sampleFormat;
    }

    if( inputChannelCount )
    {
        if( (result = TestParameters( hostApi, inputParameters, sampleRate, StreamDirection_In ))
                != paNoError )
            goto error;
    }
    if ( outputChannelCount )
    {
        if( (result = TestParameters( hostApi, outputParameters, sampleRate, StreamDirection_Out ))
                != paNoError )
            goto error;
    }

    return paFormatIsSupported;

error:
    return result;
#else
    PaError result = paFormatIsSupported;
    return result;
#endif
}

static PaError PaIoaudioStreamComponent_Initialize( PaIoaudioStreamComponent *self, PaIoaudioHostApiRepresentation *ioaudioApi,
        const PaStreamParameters *params, StreamDirection streamDir, int callbackMode )
{
    PaError result = paNoError;
    PaSampleFormat userSampleFormat = params->sampleFormat, hostSampleFormat = paNoError;
    assert( params->channelCount > 0 );

    /* Make sure things have an initial value */
    memset( self, 0, sizeof (PaIoaudioStreamComponent) );

    if( NULL == params->hostApiSpecificStreamInfo )
    {
        const PaIoaudioDeviceInfo *devInfo = GetDeviceInfo( &ioaudioApi->baseHostApiRep, params->device );
        self->numHostChannels = PA_MAX( params->channelCount, StreamDirection_In == streamDir ? devInfo->minInputChannels
                : devInfo->minOutputChannels );
    }
    else
    {
        /* We're blissfully unaware of the minimum channelCount */
        self->numHostChannels = params->channelCount;
    }

    self->device = params->device;

    PA_ENSURE( IoaudioOpen( &ioaudioApi->baseHostApiRep, params, streamDir, &self->pcm ) );
//    self->nfds = ioaudio_snd_pcm_poll_descriptors_count( self->pcm );
    self->nfds = 1;

    PA_ENSURE( hostSampleFormat = PaUtil_SelectClosestAvailableFormat( GetAvailableFormats( self->pcm ), userSampleFormat ) );

    self->hostSampleFormat = hostSampleFormat;
    self->nativeFormat = Pa2IoaudioFormat( hostSampleFormat );
    self->hostInterleaved = self->userInterleaved = !(userSampleFormat & paNonInterleaved);
    self->numUserChannels = params->channelCount;
    self->streamDir = streamDir;
    self->canMmap = 0;
    self->nonMmapBuffer = NULL;
    self->nonMmapBufferSize = 0;

    if( !callbackMode && !self->userInterleaved )
    {
        /* Pre-allocate non-interleaved user provided buffers */
        PA_UNLESS( self->userBuffers = PaUtil_AllocateMemory( sizeof (void *) * self->numUserChannels ),
                paInsufficientMemory );
    }

error:

    /* Log all available formats. */
    if ( hostSampleFormat == paSampleFormatNotSupported )
	{
        LogAllAvailableFormats( self->pcm );
        PA_DEBUG(( "%s: Please provide the log output to PortAudio developers, your hardware does not have any sample format implemented yet.\n", __FUNCTION__ ));
    }

    return result;
}

static void PaIoaudioStreamComponent_Terminate( PaIoaudioStreamComponent *self )
{
    ioaudio_snd_pcm_close( self->pcm );
    if( self->userBuffers )
        PaUtil_FreeMemory( self->userBuffers );
}

/*
static int nearbyint_(float value) {
    if(  value - (int)value > .5 )
        return (int)ceil( value );
    return (int)floor( value );
}
*/

/** Initiate configuration, preparing for determining a period size suitable for both capture and playback components.
 *
 */
static PaError PaIoaudioStreamComponent_InitialConfigure( PaIoaudioStreamComponent *self, const PaStreamParameters *params,
        int primeBuffers, snd_pcm_channel_params_t *hwParams, double *sampleRate )
{
    /* Configuration consists of setting all of IOAUDIO's parameters.
     * These parameters come in two flavors: hardware parameters
     * and software paramters.  Hardware parameters will affect
     * the way the device is initialized, software parameters
     * affect the way IOAUDIO interacts with me, the user-level client.
     */

    PaError result = paNoError;
//    snd_pcm_access_t accessMode, alternateAccessMode;
    snd_pcm_t *pcm = self->pcm;

    PA_DEBUG(( "%s mmap disabled\n", __func__ ));
    /* test if MMAP supported */
//    self->canMmap = alsa_snd_pcm_hw_params_test_access( pcm, hwParams, accessMode ) >= 0 ||
//                    alsa_snd_pcm_hw_params_test_access( pcm, hwParams, alternateAccessMode ) >= 0;

    snd_pcm_channel_info_t ch_info;
    ch_info.channel = hwParams->channel;
    result = ioaudio_snd_pcm_channel_info( pcm, &ch_info );
    if( result < 0 )
    {
        PA_DEBUG(( "snd_pcm_channel_info: %s\n", snd_strerror( result ) ));
        snd_pcm_close( pcm );
        pcm = NULL;
    }
    hwParams->mode = SND_PCM_MODE_BLOCK;
    if( SND_PCM_CHANNEL_CAPTURE == hwParams->channel )
    {
        hwParams->start_mode = SND_PCM_START_DATA;
    }
    else
    {
        hwParams->start_mode = SND_PCM_START_FULL;
    }
    hwParams->stop_mode   = SND_PCM_STOP_STOP;
    hwParams->time        = 1;
    /* Setup data stream format. */
    hwParams->format.interleave   = self->userInterleaved;
    hwParams->format.format       = self->nativeFormat;
    hwParams->format.rate         = *sampleRate;
    hwParams->format.voices       = self->numHostChannels;
    /* Setup block transfer. */
    //hwParams->buf.block.frag_size = ch_info.max_fragment_size;
    hwParams->buf.block.frag_size = 1024;
    hwParams->buf.block.frags_min = 1;
    if( SND_PCM_CHANNEL_CAPTURE == hwParams->channel )
    {
        hwParams->buf.block.frags_max = -1;
    }
    else
    {
        hwParams->buf.block.frags_max = 1;
    }
    // set periods
    // set access mode
    // set format
    // estimate sample rate
    // set channels
    result = ioaudio_snd_pcm_channel_params(pcm, hwParams);
    if(result < 0)
    {
        if(hwParams->why_failed)
        {
            PA_DEBUG(( "snd_pcm_channel_params: %d\n", hwParams->why_failed ));
        }
        else
        {
            PA_DEBUG(( "snd_pcm_channel_params: %s\n", snd_strerror( result ) ));
        }
        snd_pcm_close( pcm );
        pcm = NULL;
    }

    *sampleRate = hwParams->format.rate;

    return result;
}

/** Finish the configuration of the component's IOAUDIO device.
 *
 * As part of this method, the component's bufferSize attribute will be set.
 * @param latency: The latency for this component.
 */
static PaError PaIoaudioStreamComponent_FinishConfigure( PaIoaudioStreamComponent *self, snd_pcm_channel_params_t* hwParams,
        const PaStreamParameters *params, int primeBuffers, double sampleRate, PaTime* latency )
{
    PaError result = paNoError;
    snd_pcm_uframes_t bufSz = 0;
    *latency = -1.;
    snd_pcm_channel_setup_t csetup;

//    bufSz = params->suggestedLatency * sampleRate;
//    hwParams->buf.block.frag_size = bufSz;
    PA_DEBUG(( "%s suggestedlatency %d not reflected\n", __func__, params->suggestedLatency ));
    hwParams->buf.block.frag_size = 1024; // hardcoding for now

    /* Set the parameters! */
    {
        result = ioaudio_snd_pcm_channel_params( self->pcm, hwParams );
#ifdef PA_ENABLE_DEBUG_OUTPUT
        if( result < 0 )
        {
            if(hwParams->why_failed)
            {
                PA_DEBUG(( "snd_pcm_channel_params: %d\n", hwParams->why_failed ));
            }
            else
            {
                PA_DEBUG(( "snd_pcm_channel_params: %s\n", snd_strerror( result ) ));
            }
            ioaudio_snd_pcm_hw_params_dump( self->pcm );
        }
#endif
    }

    csetup.channel = ConvertStreamDirToChannelDir( self->streamDir );
    result = ioaudio_snd_pcm_channel_setup( self->pcm, &csetup );
#ifdef PA_ENABLE_DEBUG_OUTPUT
    if( result < 0 )
    {
        PA_DEBUG(( "snd_pcm_channel_setup: %s\n", snd_strerror( result ) ));
    }
#endif
    if( bufSz != csetup.buf.block.frag_size )
    {
        PA_DEBUG(( "frag sizes are diffrent. requested=%d, actual=%d\n", bufSz, csetup.buf.block.frag_size ));
    }
    self->bufferSize = csetup.buf.block.frag_size;

    /* Latency in seconds */
    *latency = self->bufferSize / sampleRate;

#if 0
    /* Now software parameters... */
    ENSURE_( ioaudio_snd_pcm_sw_params_current( self->pcm, swParams ), paUnanticipatedHostError );

    ENSURE_( ioaudio_snd_pcm_sw_params_set_start_threshold( self->pcm, swParams, self->framesPerBuffer ), paUnanticipatedHostError );
    ENSURE_( ioaudio_snd_pcm_sw_params_set_stop_threshold( self->pcm, swParams, self->bufferSize ), paUnanticipatedHostError );

    /* Silence buffer in the case of underrun */
    if( !primeBuffers ) /* XXX: Make sense? */
    {
        snd_pcm_uframes_t boundary;
        ENSURE_( ioaudio_snd_pcm_sw_params_get_boundary( swParams, &boundary ), paUnanticipatedHostError );
        ENSURE_( ioaudio_snd_pcm_sw_params_set_silence_threshold( self->pcm, swParams, 0 ), paUnanticipatedHostError );
        ENSURE_( ioaudio_snd_pcm_sw_params_set_silence_size( self->pcm, swParams, boundary ), paUnanticipatedHostError );
    }

    ENSURE_( ioaudio_snd_pcm_sw_params_set_avail_min( self->pcm, swParams, self->framesPerBuffer ), paUnanticipatedHostError );
    ENSURE_( ioaudio_snd_pcm_sw_params_set_xfer_align( self->pcm, swParams, 1 ), paUnanticipatedHostError );
    ENSURE_( ioaudio_snd_pcm_sw_params_set_tstamp_mode( self->pcm, swParams, SND_PCM_TSTAMP_ENABLE ), paUnanticipatedHostError );

    /* Set the parameters! */
    ENSURE_( ioaudio_snd_pcm_sw_params( self->pcm, swParams ), paUnanticipatedHostError );
#endif

    return result;
}

static PaError PaIoaudioStream_Initialize( PaIoaudioStream *self, PaIoaudioHostApiRepresentation *alsaApi, const PaStreamParameters *inParams,
        const PaStreamParameters *outParams, double sampleRate, unsigned long framesPerUserBuffer, PaStreamCallback callback,
        PaStreamFlags streamFlags, void *userData )
{
    PaError result = paNoError;
    assert( self );

    memset( self, 0, sizeof (PaIoaudioStream) );

    if( NULL != callback )
    {
        PaUtil_InitializeStreamRepresentation( &self->streamRepresentation,
                                               &alsaApi->callbackStreamInterface,
                                               callback, userData );
        self->callbackMode = 1;
    }
    else
    {
        PaUtil_InitializeStreamRepresentation( &self->streamRepresentation,
                                               &alsaApi->blockingStreamInterface,
                                               NULL, userData );
    }

    self->framesPerUserBuffer = framesPerUserBuffer;
    self->neverDropInput = streamFlags & paNeverDropInput;
    /* XXX: Ignore paPrimeOutputBuffersUsingStreamCallback untill buffer priming is fully supported in pa_process.c */
    /*
    if( outParams & streamFlags & paPrimeOutputBuffersUsingStreamCallback )
        self->primeBuffers = 1;
        */
    memset( &self->capture, 0, sizeof (PaIoaudioStreamComponent) );
    memset( &self->playback, 0, sizeof (PaIoaudioStreamComponent) );
    if( inParams )
    {
        PA_ENSURE( PaIoaudioStreamComponent_Initialize( &self->capture, alsaApi, inParams, StreamDirection_In, NULL != callback ) );
    }
    if( outParams )
    {
        PA_ENSURE( PaIoaudioStreamComponent_Initialize( &self->playback, alsaApi, outParams, StreamDirection_Out, NULL != callback ) );
    }

    assert( self->capture.nfds || self->playback.nfds );

    PA_UNLESS( self->pfds = (struct pollfd*)PaUtil_AllocateMemory( (self->capture.nfds +
                    self->playback.nfds) * sizeof (struct pollfd) ), paInsufficientMemory );

    PaUtil_InitializeCpuLoadMeasurer( &self->cpuLoadMeasurer, sampleRate );
    ASSERT_CALL_( PaUnixMutex_Initialize( &self->stateMtx ), paNoError );

error:
    return result;
}

/** Free resources associated with stream, and eventually stream itself.
 *
 * Frees allocated memory, and terminates individual StreamComponents.
 */
static void PaIoaudioStream_Terminate( PaIoaudioStream *self )
{
    assert( self );

    if( self->capture.pcm )
    {
        PaIoaudioStreamComponent_Terminate( &self->capture );
    }
    if( self->playback.pcm )
    {
        PaIoaudioStreamComponent_Terminate( &self->playback );
    }

    PaUtil_FreeMemory( self->pfds );
    ASSERT_CALL_( PaUnixMutex_Terminate( &self->stateMtx ), paNoError );

    PaUtil_FreeMemory( self );
}

/** Calculate polling timeout
 *
 * @param frames Time to wait
 * @return Polling timeout in milliseconds
 */
static int CalculatePollTimeout( const PaIoaudioStream *stream, unsigned long frames )
{
    assert( stream->streamRepresentation.streamInfo.sampleRate > 0.0 );
    /* Period in msecs, rounded up */
    return (int)ceil( 1000 * frames / stream->streamRepresentation.streamInfo.sampleRate );
}

#if 0
/** Align value in backward direction.
 *
 * @param v: Value to align.
 * @param align: Alignment.
 */
static unsigned long PaIoaudio_AlignBackward(unsigned long v, unsigned long align)
{
	return ((v - (align ? v % align : 0)));
}
#endif

#if 0
/** Align value in forward direction.
 *
 * @param v: Value to align.
 * @param align: Alignment.
 */
static unsigned long PaIoaudio_AlignForward(unsigned long v, unsigned long align)
{
	unsigned long remainder = (align ? (v % align) : 0);
	return (remainder != 0 ? v + (align - remainder) : v);
}
#endif

#if 0
/** Get size of host buffer maintained from the number of user frames, sample rate and suggested latency. Minimum double buffering
 *  is maintained to allow 100% CPU usage inside user callback.
 *
 * @param userFramesPerBuffer: User buffer size in number of frames.
 * @param suggestedLatency: User provided desired latency.
 * @param sampleRate: Sample rate.
 */
static unsigned long PaIoaudio_GetFramesPerHostBuffer(unsigned long userFramesPerBuffer, PaTime suggestedLatency, double sampleRate)
{
	unsigned long frames = userFramesPerBuffer + PA_MAX( userFramesPerBuffer, (unsigned long)(suggestedLatency * sampleRate) );
    PA_DEBUG(( "%s frames=%d\n", __func__, frames ));
    return frames;
}
#endif

#if 0
/** Determine size per host buffer.
 *
 * During this method call, the component's framesPerBuffer attribute gets computed, and the corresponding period size
 * gets configured for the device.
 * @param accurate: If the configured period size is non-integer, this will be set to 0.
 */
static PaError PaIoaudioStreamComponent_DetermineFramesPerBuffer( PaIoaudioStreamComponent* self, const PaStreamParameters* params,
        unsigned long framesPerUserBuffer, double sampleRate, snd_pcm_channel_params_t* hwParams, int* accurate )
{
    PaError result = paNoError;
//    unsigned long bufferSize, framesPerHostBuffer;
    unsigned long framesPerHostBuffer;
//    int dir = 0;

    framesPerHostBuffer = 1024;
    PA_DEBUG(( "%s host buffer size calculation disabled\n", __func__ ));
    /* Calculate host buffer size */
    bufferSize = PaIoaudio_GetFramesPerHostBuffer(framesPerUserBuffer, params->suggestedLatency, sampleRate);

    /* Log */
    PA_DEBUG(( "%s: user-buffer (frames)           = %lu\n", __FUNCTION__, framesPerUserBuffer ));
    PA_DEBUG(( "%s: user-buffer (sec)              = %f\n",  __FUNCTION__, (double)(framesPerUserBuffer / sampleRate) ));
    PA_DEBUG(( "%s: suggested latency (sec)        = %f\n",  __FUNCTION__, params->suggestedLatency ));
    PA_DEBUG(( "%s: suggested host buffer (frames) = %lu\n", __FUNCTION__, bufferSize ));
    PA_DEBUG(( "%s: suggested host buffer (sec)    = %f\n",  __FUNCTION__, (double)(bufferSize / sampleRate) ));


	{
        unsigned numPeriods = numPeriods_, maxPeriods = 0, minPeriods = numPeriods_;

        /* It may be that the device only supports 2 periods for instance */
        dir = 0;
        ENSURE_( ioaudio_snd_pcm_hw_params_get_periods_min( hwParams, &minPeriods, &dir ), paUnanticipatedHostError );
        ENSURE_( ioaudio_snd_pcm_hw_params_get_periods_max( hwParams, &maxPeriods, &dir ), paUnanticipatedHostError );
        assert( maxPeriods > 1 );

        /* Clamp to min/max */
        numPeriods = PA_MIN(maxPeriods, PA_MAX(minPeriods, numPeriods));

        PA_DEBUG(( "%s: periods min = %lu, max = %lu, req = %lu \n", __FUNCTION__, minPeriods, maxPeriods, numPeriods ));

#ifndef PA_IOAUDIO_USE_OBSOLETE_HOST_BUFFER_CALC

        /* Calculate period size */
        framesPerHostBuffer = (bufferSize / numPeriods);

        /* Align & test size */
        if( framesPerUserBuffer != paFramesPerBufferUnspecified )
        {
            /* Align to user buffer size */
            framesPerHostBuffer = PaIoaudio_AlignForward(framesPerHostBuffer, framesPerUserBuffer);

            /* Test (borrowed from older implementation) */
            if( framesPerHostBuffer < framesPerUserBuffer )
            {
                assert( framesPerUserBuffer % framesPerHostBuffer == 0 );
                if( ioaudio_snd_pcm_channel_test_period_size( self->pcm, hwParams, framesPerHostBuffer, 0 ) < 0 )
                {
                    if( ioaudio_snd_pcm_channel_test_period_size( self->pcm, hwParams, framesPerHostBuffer * 2, 0 ) == 0 )
                        framesPerHostBuffer *= 2;
                    else
                    if( ioaudio_snd_pcm_channel_test_period_size( self->pcm, hwParams, framesPerHostBuffer / 2, 0 ) == 0 )
                        framesPerHostBuffer /= 2;
                }
            }
            else
            {
                assert( framesPerHostBuffer % framesPerUserBuffer == 0 );
                if( ioaudio_snd_pcm_channel_test_period_size( self->pcm, hwParams, framesPerHostBuffer, 0 ) < 0 )
                {
                    if( ioaudio_snd_pcm_channel_test_period_size( self->pcm, hwParams, framesPerHostBuffer + framesPerUserBuffer, 0 ) == 0 )
                        framesPerHostBuffer += framesPerUserBuffer;
                    else
                    if( ioaudio_snd_pcm_channel_test_period_size( self->pcm, hwParams, framesPerHostBuffer - framesPerUserBuffer, 0 ) == 0 )
                        framesPerHostBuffer -= framesPerUserBuffer;
                }
            }
        }
#endif

        PA_DEBUG(( "%s: suggested host buffer period   = %lu \n", __FUNCTION__, framesPerHostBuffer ));
	}

    {
        /* Get min/max period sizes and adjust our chosen */
        snd_pcm_uframes_t min = 0, max = 0, minmax_diff;
        ENSURE_( ioaudio_snd_pcm_hw_params_get_period_size_min( hwParams, &min, NULL ), paUnanticipatedHostError );
        ENSURE_( ioaudio_snd_pcm_hw_params_get_period_size_max( hwParams, &max, NULL ), paUnanticipatedHostError );
        minmax_diff = max - min;

        if( framesPerHostBuffer < min )
        {
            PA_DEBUG(( "%s: The determined period size (%lu) is less than minimum (%lu)\n", __FUNCTION__, framesPerHostBuffer, min ));
            framesPerHostBuffer = ((minmax_diff == 2) ? min + 1 : min);
        }
        else
        if( framesPerHostBuffer > max )
        {
            PA_DEBUG(( "%s: The determined period size (%lu) is greater than maximum (%lu)\n", __FUNCTION__, framesPerHostBuffer, max ));
            framesPerHostBuffer = ((minmax_diff == 2) ? max - 1 : max);
        }

		PA_DEBUG(( "%s: device period minimum          = %lu\n", __FUNCTION__, min ));
		PA_DEBUG(( "%s: device period maximum          = %lu\n", __FUNCTION__, max ));
		PA_DEBUG(( "%s: host buffer period             = %lu\n", __FUNCTION__, framesPerHostBuffer ));
		PA_DEBUG(( "%s: host buffer period latency     = %f\n", __FUNCTION__, (double)(framesPerHostBuffer / sampleRate) ));

        /* Try setting period size */
        dir = 0;
        ENSURE_( ioaudio_snd_pcm_hw_params_set_period_size_near( self->pcm, hwParams, &framesPerHostBuffer, &dir ), paUnanticipatedHostError );
        if( dir != 0 )
        {
            PA_DEBUG(( "%s: The configured period size is non-integer.\n", __FUNCTION__, dir ));
            *accurate = 0;
        }
    }

    /* Set result */
    self->framesPerBuffer = framesPerHostBuffer;

    return result;
}
#endif

/* We need to determine how many frames per host buffer (period) to use.  Our
 * goals are to provide the best possible performance, but also to
 * honor the requested latency settings as closely as we can. Therefore this
 * decision is based on:
 *
 *   - the period sizes that playback and/or capture support.  The
 *     host buffer size has to be one of these.
 *   - the number of periods that playback and/or capture support.
 *
 * We want to make period_size*(num_periods-1) to be as close as possible
 * to latency*rate for both playback and capture.
 *
 * This method will determine suitable period sizes for capture and playback handles, and report the maximum number of
 * frames per host buffer. The latter is relevant, in case we should be so unfortunate that the period size differs
 * between capture and playback. If this should happen, the stream's hostBufferSizeMode attribute will be set to
 * paUtilBoundedHostBufferSize, because the best we can do is limit the size of individual host buffers to the upper
 * bound. The size of host buffers scheduled for processing should only matter if the user has specified a buffer size,
 * but when he/she does we must strive for an optimal configuration. By default we'll opt for a fixed host buffer size,
 * which should be fine if the period size is the same for capture and playback. In general, if there is a specified user
 * buffer size, this method tries it best to determine a period size which is a multiple of the user buffer size.
 *
 * The framesPerBuffer attributes of the individual capture and playback components of the stream are set to corresponding
 * values determined here. Since these should be reported as
 *
 * This is one of those blocks of code that will just take a lot of
 * refinement to be any good.
 *
 * In the full-duplex case it is possible that the routine was unable
 * to find a number of frames per buffer acceptable to both devices
 * TODO: Implement an algorithm to find the value closest to acceptance
 * by both devices, to minimize difference between period sizes?
 *
 * @param determinedFramesPerHostBuffer: The determined host buffer size.
 */
static PaError PaIoaudioStream_DetermineFramesPerBuffer( PaIoaudioStream* self, double sampleRate, const PaStreamParameters* inputParameters,
        const PaStreamParameters* outputParameters, unsigned long framesPerUserBuffer, snd_pcm_channel_params_t* hwParamsCapture,
        snd_pcm_channel_params_t* hwParamsPlayback, PaUtilHostBufferSizeMode* hostBufferSizeMode )
{
    PaError result = paNoError;

    *hostBufferSizeMode = paUtilFixedHostBufferSize;
    self->maxFramesPerHostBuffer = 1024;
    self->playback.framesPerBuffer = 1024;
    self->capture.framesPerBuffer = 1024;
    PA_DEBUG(( "%s disabled\n", __func__ ));
#if 0
    unsigned long framesPerHostBuffer = 0;
    int dir = 0;
    int accurate = 1;
    unsigned numPeriods = numPeriods_;

    if( self->capture.pcm && self->playback.pcm )
    {
        if( framesPerUserBuffer == paFramesPerBufferUnspecified )
        {
            /* Come up with a common desired latency */
            snd_pcm_uframes_t desiredBufSz, e, minPeriodSize, maxPeriodSize, optimalPeriodSize, periodSize,
                              minCapture, minPlayback, maxCapture, maxPlayback;

            dir = 0;
            ENSURE_( ioaudio_snd_pcm_hw_params_get_period_size_min( hwParamsCapture, &minCapture, &dir ), paUnanticipatedHostError );
            dir = 0;
            ENSURE_( ioaudio_snd_pcm_hw_params_get_period_size_min( hwParamsPlayback, &minPlayback, &dir ), paUnanticipatedHostError );
            dir = 0;
            ENSURE_( ioaudio_snd_pcm_hw_params_get_period_size_max( hwParamsCapture, &maxCapture, &dir ), paUnanticipatedHostError );
            dir = 0;
            ENSURE_( ioaudio_snd_pcm_hw_params_get_period_size_max( hwParamsPlayback, &maxPlayback, &dir ), paUnanticipatedHostError );
            minPeriodSize = PA_MAX( minPlayback, minCapture );
            maxPeriodSize = PA_MIN( maxPlayback, maxCapture );
            PA_UNLESS( minPeriodSize <= maxPeriodSize, paBadIODeviceCombination );

            desiredBufSz = (snd_pcm_uframes_t)(PA_MIN( outputParameters->suggestedLatency, inputParameters->suggestedLatency )
                    * sampleRate);
            /* Clamp desiredBufSz */
            {
                snd_pcm_uframes_t maxBufferSize;
                snd_pcm_uframes_t maxBufferSizeCapture, maxBufferSizePlayback;
                ENSURE_( ioaudio_snd_pcm_hw_params_get_buffer_size_max( hwParamsCapture, &maxBufferSizeCapture ), paUnanticipatedHostError );
                ENSURE_( ioaudio_snd_pcm_hw_params_get_buffer_size_max( hwParamsPlayback, &maxBufferSizePlayback ), paUnanticipatedHostError );
                maxBufferSize = PA_MIN( maxBufferSizeCapture, maxBufferSizePlayback );

                desiredBufSz = PA_MIN( desiredBufSz, maxBufferSize );
            }

            /* Find the closest power of 2 */
            e = ilogb( minPeriodSize );
            if( minPeriodSize & (minPeriodSize - 1) )
                e += 1;
            periodSize = (snd_pcm_uframes_t)pow( 2, e );

            while( periodSize <= maxPeriodSize )
            {
                if( ioaudio_snd_pcm_channel_test_period_size( self->playback.pcm, hwParamsPlayback, periodSize, 0 ) >= 0 &&
                        ioaudio_snd_pcm_channel_test_period_size( self->capture.pcm, hwParamsCapture, periodSize, 0 ) >= 0 )
                {
                    /* OK! */
                    break;
                }

                periodSize *= 2;
            }

            optimalPeriodSize = PA_MAX( desiredBufSz / numPeriods, minPeriodSize );
            optimalPeriodSize = PA_MIN( optimalPeriodSize, maxPeriodSize );

            /* Find the closest power of 2 */
            e = ilogb( optimalPeriodSize );
            if( optimalPeriodSize & (optimalPeriodSize - 1) )
                e += 1;
            optimalPeriodSize = (snd_pcm_uframes_t)pow( 2, e );

            while( optimalPeriodSize >= periodSize )
            {
                if( ioaudio_snd_pcm_channel_test_period_size( self->capture.pcm, hwParamsCapture, optimalPeriodSize, 0 )
                        >= 0 && ioaudio_snd_pcm_channel_test_period_size( self->playback.pcm, hwParamsPlayback,
                            optimalPeriodSize, 0 ) >= 0 )
                {
                    break;
                }
                optimalPeriodSize /= 2;
            }

            if( optimalPeriodSize > periodSize )
                periodSize = optimalPeriodSize;

            if( periodSize <= maxPeriodSize )
            {
                /* Looks good, the periodSize _should_ be acceptable by both devices */
                ENSURE_( ioaudio_snd_pcm_hw_params_set_period_size( self->capture.pcm, hwParamsCapture, periodSize, 0 ),
                        paUnanticipatedHostError );
                ENSURE_( ioaudio_snd_pcm_hw_params_set_period_size( self->playback.pcm, hwParamsPlayback, periodSize, 0 ),
                        paUnanticipatedHostError );
                self->capture.framesPerBuffer = self->playback.framesPerBuffer = periodSize;
                framesPerHostBuffer = periodSize;
            }
            else
            {
                /* Unable to find a common period size, oh well */
                optimalPeriodSize = PA_MAX( desiredBufSz / numPeriods, minPeriodSize );
                optimalPeriodSize = PA_MIN( optimalPeriodSize, maxPeriodSize );

                self->capture.framesPerBuffer = optimalPeriodSize;
                dir = 0;
                ENSURE_( ioaudio_snd_pcm_hw_params_set_period_size_near( self->capture.pcm, hwParamsCapture, &self->capture.framesPerBuffer, &dir ),
                        paUnanticipatedHostError );
                self->playback.framesPerBuffer = optimalPeriodSize;
                dir = 0;
                ENSURE_( ioaudio_snd_pcm_hw_params_set_period_size_near( self->playback.pcm, hwParamsPlayback, &self->playback.framesPerBuffer, &dir ),
                        paUnanticipatedHostError );
                framesPerHostBuffer = PA_MAX( self->capture.framesPerBuffer, self->playback.framesPerBuffer );
                *hostBufferSizeMode = paUtilBoundedHostBufferSize;
            }
        }
        else
        {
            /* We choose the simple route and determine a suitable number of frames per buffer for one component of
             * the stream, then we hope that this will work for the other component too (it should!).
             */

            unsigned maxPeriods = 0;
            PaIoaudioStreamComponent* first = &self->capture, * second = &self->playback;
            const PaStreamParameters* firstStreamParams = inputParameters;
            snd_pcm_channel_params_t* firstHwParams = hwParamsCapture, * secondHwParams = hwParamsPlayback;

            dir = 0;
            ENSURE_( ioaudio_snd_pcm_hw_params_get_periods_max( hwParamsPlayback, &maxPeriods, &dir ), paUnanticipatedHostError );
            if( maxPeriods < numPeriods )
            {
                /* The playback component is trickier to get right, try that first */
                first = &self->playback;
                second = &self->capture;
                firstStreamParams = outputParameters;
                firstHwParams = hwParamsPlayback;
                secondHwParams = hwParamsCapture;
            }

            PA_ENSURE( PaIoaudioStreamComponent_DetermineFramesPerBuffer( first, firstStreamParams, framesPerUserBuffer,
                        sampleRate, firstHwParams, &accurate ) );

            second->framesPerBuffer = first->framesPerBuffer;
            dir = 0;
            ENSURE_( ioaudio_snd_pcm_hw_params_set_period_size_near( second->pcm, secondHwParams, &second->framesPerBuffer, &dir ),
                    paUnanticipatedHostError );
            if( self->capture.framesPerBuffer == self->playback.framesPerBuffer )
            {
                framesPerHostBuffer = self->capture.framesPerBuffer;
            }
            else
            {
                framesPerHostBuffer = PA_MAX( self->capture.framesPerBuffer, self->playback.framesPerBuffer );
                *hostBufferSizeMode = paUtilBoundedHostBufferSize;
            }
        }
    }
    else    /* half-duplex is a slightly simpler case */
    {
        if( self->capture.pcm )
        {
            PA_ENSURE( PaIoaudioStreamComponent_DetermineFramesPerBuffer( &self->capture, inputParameters, framesPerUserBuffer,
                        sampleRate, hwParamsCapture, &accurate) );
            framesPerHostBuffer = self->capture.framesPerBuffer;
        }
        else
        {
            assert( self->playback.pcm );
            PA_ENSURE( PaIoaudioStreamComponent_DetermineFramesPerBuffer( &self->playback, outputParameters, framesPerUserBuffer,
                        sampleRate, hwParamsPlayback, &accurate ) );
            framesPerHostBuffer = self->playback.framesPerBuffer;
        }
    }

    PA_UNLESS( framesPerHostBuffer != 0, paInternalError );
    self->maxFramesPerHostBuffer = framesPerHostBuffer;

    if( !self->playback.canMmap || !accurate )
    {
        /* Don't know the exact size per host buffer */
        *hostBufferSizeMode = paUtilBoundedHostBufferSize;
        /* Raise upper bound */
        if( !accurate )
            ++self->maxFramesPerHostBuffer;
    }
#endif

    return result;
}

/** Set up IOAUDIO stream parameters.
 *
 */
static PaError PaIoaudioStream_Configure( PaIoaudioStream *self, const PaStreamParameters *inParams, const PaStreamParameters*
        outParams, double sampleRate, unsigned long framesPerUserBuffer, double* inputLatency, double* outputLatency,
        PaUtilHostBufferSizeMode* hostBufferSizeMode )
{
    PaError result = paNoError;
    double realSr = sampleRate;
    snd_pcm_channel_params_t hwParamsCapture, hwParamsPlayback;

    memset(&hwParamsCapture, 0, sizeof(snd_pcm_channel_params_t));
    memset(&hwParamsPlayback, 0, sizeof(snd_pcm_channel_params_t));

    if( self->capture.pcm )
    {
        hwParamsCapture.channel = SND_PCM_CHANNEL_CAPTURE;
        PA_ENSURE( PaIoaudioStreamComponent_InitialConfigure( &self->capture, inParams, self->primeBuffers, &hwParamsCapture,
                    &realSr ) );
    }
    if( self->playback.pcm )
    {
        hwParamsCapture.channel = SND_PCM_CHANNEL_PLAYBACK;
        PA_ENSURE( PaIoaudioStreamComponent_InitialConfigure( &self->playback, outParams, self->primeBuffers, &hwParamsPlayback,
                    &realSr ) );
    }

    PA_ENSURE( PaIoaudioStream_DetermineFramesPerBuffer( self, realSr, inParams, outParams, framesPerUserBuffer,
                &hwParamsCapture, &hwParamsPlayback, hostBufferSizeMode ) );

    if( self->capture.pcm )
    {
        assert( self->capture.framesPerBuffer != 0 );
        PA_ENSURE( PaIoaudioStreamComponent_FinishConfigure( &self->capture, &hwParamsCapture, inParams, self->primeBuffers, realSr,
                    inputLatency ) );
        PA_DEBUG(( "%s: Capture period size: %lu, latency: %f\n", __FUNCTION__, self->capture.framesPerBuffer, *inputLatency ));
    }
    if( self->playback.pcm )
    {
        assert( self->playback.framesPerBuffer != 0 );
        PA_ENSURE( PaIoaudioStreamComponent_FinishConfigure( &self->playback, &hwParamsPlayback, outParams, self->primeBuffers, realSr,
                    outputLatency ) );
        PA_DEBUG(( "%s: Playback period size: %lu, latency: %f\n", __FUNCTION__, self->playback.framesPerBuffer, *outputLatency ));
    }

    /* Should be exact now */
    self->streamRepresentation.streamInfo.sampleRate = realSr;

    /* this will cause the two streams to automatically start/stop/prepare in sync.
     * We only need to execute these operations on one of the pair.
     * A: We don't want to do this on a blocking stream.
     */
    if( self->callbackMode && self->capture.pcm && self->playback.pcm )
    {
        int err = ioaudio_snd_pcm_link( self->capture.pcm, self->playback.pcm );
        if( err == 0 )
            self->pcmsSynced = 1;
        else
            PA_DEBUG(( "%s: Unable to sync pcms: %s\n", __FUNCTION__, ioaudio_snd_strerror( err ) ));
    }

    {
        unsigned long minFramesPerHostBuffer = PA_MIN( self->capture.pcm ? self->capture.framesPerBuffer : ULONG_MAX,
            self->playback.pcm ? self->playback.framesPerBuffer : ULONG_MAX );
        self->pollTimeout = CalculatePollTimeout( self, minFramesPerHostBuffer );    /* Period in msecs, rounded up */

        /* Time before watchdog unthrottles realtime thread == 1/4 of period time in msecs */
        /* self->threading.throttledSleepTime = (unsigned long) (minFramesPerHostBuffer / sampleRate / 4 * 1000); */
    }

    if( self->callbackMode )
    {
        /* If the user expects a certain number of frames per callback we will either have to rely on block adaption
         * (framesPerHostBuffer is not an integer multiple of framesPerBuffer) or we can simply align the number
         * of host buffer frames with what the user specified */
        if( self->framesPerUserBuffer != paFramesPerBufferUnspecified )
        {
            /* self->alignFrames = 1; */

            /* Unless the ratio between number of host and user buffer frames is an integer we will have to rely
             * on block adaption */
        /*
            if( framesPerHostBuffer % framesPerBuffer != 0 || (self->capture.pcm && self->playback.pcm &&
                        self->capture.framesPerBuffer != self->playback.framesPerBuffer) )
                self->useBlockAdaption = 1;
            else
                self->alignFrames = 1;
        */
        }
    }

error:
    return result;
}

static PaError OpenStream( struct PaUtilHostApiRepresentation *hostApi,
                           PaStream** s,
                           const PaStreamParameters *inputParameters,
                           const PaStreamParameters *outputParameters,
                           double sampleRate,
                           unsigned long framesPerBuffer,
                           PaStreamFlags streamFlags,
                           PaStreamCallback* callback,
                           void *userData )
{
    PaError result = paNoError;
    PaIoaudioHostApiRepresentation *ioaudioHostApi = (PaIoaudioHostApiRepresentation*)hostApi;
    PaIoaudioStream *stream = NULL;
    PaSampleFormat hostInputSampleFormat = 0, hostOutputSampleFormat = 0;
    PaSampleFormat inputSampleFormat = 0, outputSampleFormat = 0;
    int numInputChannels = 0, numOutputChannels = 0;
    PaTime inputLatency, outputLatency;
    /* Operate with fixed host buffer size by default, since other modes will invariably lead to block adaption */
    /* XXX: Use Bounded by default? Output tends to get stuttery with Fixed ... */
    PaUtilHostBufferSizeMode hostBufferSizeMode = paUtilFixedHostBufferSize;

    if( (streamFlags & paPlatformSpecificFlags) != 0 )
        return paInvalidFlag;

    if( inputParameters )
    {
        PA_ENSURE( ValidateParameters( inputParameters, hostApi, StreamDirection_In ) );

        numInputChannels = inputParameters->channelCount;
        inputSampleFormat = inputParameters->sampleFormat;
    }
    if( outputParameters )
    {
        PA_ENSURE( ValidateParameters( outputParameters, hostApi, StreamDirection_Out ) );

        numOutputChannels = outputParameters->channelCount;
        outputSampleFormat = outputParameters->sampleFormat;
    }

    /* XXX: Why do we support this anyway? */
    if( framesPerBuffer == paFramesPerBufferUnspecified && getenv( "PA_IOAUDIO_PERIODSIZE" ) != NULL )
    {
        PA_DEBUG(( "%s: Getting framesPerBuffer from environment\n", __FUNCTION__ ));
        framesPerBuffer = atoi( getenv("PA_IOAUDIO_PERIODSIZE") );
    }

    PA_UNLESS( stream = (PaIoaudioStream*)PaUtil_AllocateMemory( sizeof(PaIoaudioStream) ), paInsufficientMemory );
    PA_ENSURE( PaIoaudioStream_Initialize( stream, ioaudioHostApi, inputParameters, outputParameters, sampleRate,
                framesPerBuffer, callback, streamFlags, userData ) );

    PA_ENSURE( PaIoaudioStream_Configure( stream, inputParameters, outputParameters, sampleRate, framesPerBuffer,
                &inputLatency, &outputLatency, &hostBufferSizeMode ) );
    hostInputSampleFormat = stream->capture.hostSampleFormat | (!stream->capture.hostInterleaved ? paNonInterleaved : 0);
    hostOutputSampleFormat = stream->playback.hostSampleFormat | (!stream->playback.hostInterleaved ? paNonInterleaved : 0);

    PA_ENSURE( PaUtil_InitializeBufferProcessor( &stream->bufferProcessor,
                    numInputChannels, inputSampleFormat, hostInputSampleFormat,
                    numOutputChannels, outputSampleFormat, hostOutputSampleFormat,
                    sampleRate, streamFlags, framesPerBuffer, stream->maxFramesPerHostBuffer,
                    hostBufferSizeMode, callback, userData ) );

    /* Ok, buffer processor is initialized, now we can deduce it's latency */
    if( numInputChannels > 0 )
        stream->streamRepresentation.streamInfo.inputLatency = inputLatency + (PaTime)(
                PaUtil_GetBufferProcessorInputLatencyFrames( &stream->bufferProcessor ) / sampleRate);
    if( numOutputChannels > 0 )
        stream->streamRepresentation.streamInfo.outputLatency = outputLatency + (PaTime)(
                PaUtil_GetBufferProcessorOutputLatencyFrames( &stream->bufferProcessor ) / sampleRate);

    PA_DEBUG(( "%s: Stream: framesPerBuffer = %lu, maxFramesPerHostBuffer = %lu, latency = i(%f)/o(%f), \n", __FUNCTION__, framesPerBuffer, stream->maxFramesPerHostBuffer, stream->streamRepresentation.streamInfo.inputLatency, stream->streamRepresentation.streamInfo.outputLatency));

    *s = (PaStream*)stream;

    return result;

error:
    if( stream )
    {
        PA_DEBUG(( "%s: Stream in error, terminating\n", __FUNCTION__ ));
        PaIoaudioStream_Terminate( stream );
    }

    return result;
}

static PaError CloseStream( PaStream* s )
{
    PaError result = paNoError;
    PaIoaudioStream *stream = (PaIoaudioStream*)s;

    PaUtil_TerminateBufferProcessor( &stream->bufferProcessor );
    PaUtil_TerminateStreamRepresentation( &stream->streamRepresentation );

    PaIoaudioStream_Terminate( stream );

    return result;
}

static void SilenceBuffer( PaIoaudioStream *stream )
{
    //const snd_pcm_channel_area_t *areas;
    snd_pcm_channel_status_t status;
    //snd_pcm_uframes_t frames, offset;
    snd_pcm_uframes_t frames;

    status.channel = SND_PCM_CHANNEL_PLAYBACK;
    ioaudio_snd_pcm_channel_status( stream->playback.pcm, &status );
    frames = (snd_pcm_uframes_t)status.free;

    unsigned char *buffer;
    int bufsize = ioaudio_snd_pcm_format_size( stream->playback.nativeFormat, frames );
    buffer = calloc(bufsize, sizeof(unsigned char));

    ssize_t wbytes = ioaudio_snd_pcm_write( stream->playback.pcm, buffer, bufsize);
    if( frames != wbytes )
    {
        PA_DEBUG(( "%s frames=%d != wbytes=%d\n", frames, wbytes ));
    }

    free(buffer);
}

/** Start/prepare pcm(s) for streaming.
 *
 * Depending on wether the stream is in callback or blocking mode, we will respectively start or simply
 * prepare the playback pcm. If the buffer has _not_ been primed, we will in callback mode prepare and
 * silence the buffer before starting playback. In blocking mode we simply prepare, as the playback will
 * be started automatically as the user writes to output.
 *
 * The capture pcm, however, will simply be prepared and started.
 */
static PaError IoaudioStart( PaIoaudioStream *stream, int priming )
{
    PaError result = paNoError;

    if( stream->playback.pcm )
    {
        if( stream->callbackMode )
        {
            if( !priming )
            {
                /* Buffer isn't primed, so prepare and silence */
                ENSURE_( ioaudio_snd_pcm_channel_prepare( stream->playback.pcm, SND_PCM_CHANNEL_PLAYBACK ), paUnanticipatedHostError );
                if( stream->playback.canMmap )
                    SilenceBuffer( stream );
            }
//            if( stream->playback.canMmap )
//                ENSURE_( alsa_snd_pcm_start( stream->playback.pcm ), paUnanticipatedHostError );
        }
        else
            ENSURE_( ioaudio_snd_pcm_channel_prepare( stream->playback.pcm, SND_PCM_CHANNEL_PLAYBACK ), paUnanticipatedHostError );
    }
    if( stream->capture.pcm && !stream->pcmsSynced )
    {
        ENSURE_( ioaudio_snd_pcm_channel_prepare( stream->capture.pcm, SND_PCM_CHANNEL_CAPTURE ), paUnanticipatedHostError );
        /* For a blocking stream we want to start capture as well, since nothing will happen otherwise */
//        ENSURE_( ioaudio_snd_pcm_start( stream->capture.pcm ), paUnanticipatedHostError );
    }

end:
    return result;
error:
    goto end;
}

/** Utility function for determining if pcms are in running state.
 *
 */
#if 0
static int IsRunning( PaIoaudioStream *stream )
{
    int result = 0;

    PA_ENSURE( PaUnixMutex_Lock( &stream->stateMtx ) );
    if( stream->capture.pcm )
    {
        snd_pcm_state_t capture_state = ioaudio_snd_pcm_state( stream->capture.pcm );

        if( capture_state == SND_PCM_STATE_RUNNING || capture_state == SND_PCM_STATE_XRUN
                || capture_state == SND_PCM_STATE_DRAINING )
        {
            result = 1;
            goto end;
        }
    }

    if( stream->playback.pcm )
    {
        snd_pcm_state_t playback_state = ioaudio_snd_pcm_state( stream->playback.pcm );

        if( playback_state == SND_PCM_STATE_RUNNING || playback_state == SND_PCM_STATE_XRUN
                || playback_state == SND_PCM_STATE_DRAINING )
        {
            result = 1;
            goto end;
        }
    }

end:
    ASSERT_CALL_( PaUnixMutex_Unlock( &stream->stateMtx ), paNoError );
    return result;
error:
    goto error;
}
#endif

static PaError StartStream( PaStream *s )
{
    PaError result = paNoError;
    PaIoaudioStream* stream = (PaIoaudioStream*)s;
    int streamStarted = 0;  /* So we can know wether we need to take the stream down */

    /* Ready the processor */
    PaUtil_ResetBufferProcessor( &stream->bufferProcessor );

    /* Set now, so we can test for activity further down */
    stream->isActive = 1;

    if( stream->callbackMode )
    {
        PA_ENSURE( PaUnixThread_New( &stream->thread, &CallbackThreadFunc, stream, 1., stream->rtSched ) );
    }
    else
    {
        PA_ENSURE( IoaudioStart( stream, 0 ) );
        streamStarted = 1;
    }

end:
    return result;
error:
    if( streamStarted )
    {
        AbortStream( stream );
    }
    stream->isActive = 0;

    goto end;
}

/** Stop PCM handle, either softly or abruptly.
 */
static PaError IoaudioStop( PaIoaudioStream *stream, int abort )
{
    PaError result = paNoError;
    /* XXX: ioaudio_snd_pcm_drain tends to lock up, avoid it until we find out more */
    abort = 1;
    /*
    if( stream->capture.pcm && !strcmp( Pa_GetDeviceInfo( stream->capture.device )->name,
                "dmix" ) )
    {
        abort = 1;
    }
    else if( stream->playback.pcm && !strcmp( Pa_GetDeviceInfo( stream->playback.device )->name,
                "dmix" ) )
    {
        abort = 1;
    }
    */

    if( abort )
    {
        if( stream->playback.pcm )
        {
            ENSURE_( ioaudio_snd_pcm_channel_flush( stream->playback.pcm, SND_PCM_CHANNEL_PLAYBACK ), paUnanticipatedHostError );
        }
        if( stream->capture.pcm && !stream->pcmsSynced )
        {
            ENSURE_( ioaudio_snd_pcm_channel_flush( stream->capture.pcm, SND_PCM_CHANNEL_CAPTURE ), paUnanticipatedHostError );
        }

        PA_DEBUG(( "%s: Dropped frames\n", __FUNCTION__ ));
    }
    else
    {
        if( stream->playback.pcm )
        {
            ENSURE_( ioaudio_snd_pcm_nonblock_mode( stream->playback.pcm, 0 ), paUnanticipatedHostError );
            if( ioaudio_snd_pcm_playback_drain( stream->playback.pcm ) < 0 )
            {
                PA_DEBUG(( "%s: Draining playback handle failed!\n", __FUNCTION__ ));
            }
        }
        if( stream->capture.pcm && !stream->pcmsSynced )
        {
            /* We don't need to retrieve any remaining frames */
            if( ioaudio_snd_pcm_channel_flush( stream->capture.pcm, SND_PCM_CHANNEL_CAPTURE ) < 0 )
            {
                PA_DEBUG(( "%s: Flushing capture handle failed!\n", __FUNCTION__ ));
            }
        }
    }

end:
    return result;
error:
    goto end;
}

/** Stop or abort stream.
 *
 * If a stream is in callback mode we will have to inspect wether the background thread has
 * finished, or we will have to take it out. In either case we join the thread before
 * returning. In blocking mode, we simply tell IOAUDIO to stop abruptly (abort) or finish
 * buffers (drain)
 *
 * Stream will be considered inactive (!PaIoaudioStream::isActive) after a call to this function
 */
static PaError RealStop( PaIoaudioStream *stream, int abort )
{
    PaError result = paNoError;

    /* First deal with the callback thread, cancelling and/or joining
     * it if necessary
     */
    if( stream->callbackMode )
    {
        PaError threadRes;
        stream->callbackAbort = abort;

        if( !abort )
        {
            PA_DEBUG(( "Stopping callback\n" ));
        }
        PA_ENSURE( PaUnixThread_Terminate( &stream->thread, !abort, &threadRes ) );
        if( threadRes != paNoError )
        {
            PA_DEBUG(( "Callback thread returned: %d\n", threadRes ));
        }
#if 0
        if( watchdogRes != paNoError )
            PA_DEBUG(( "Watchdog thread returned: %d\n", watchdogRes ));
#endif

        stream->callback_finished = 0;
    }
    else
    {
        PA_ENSURE( IoaudioStop( stream, abort ) );
    }

    stream->isActive = 0;

end:
    return result;

error:
    goto end;
}

static PaError StopStream( PaStream *s )
{
    return RealStop( (PaIoaudioStream *) s, 0 );
}

static PaError AbortStream( PaStream *s )
{
    return RealStop( (PaIoaudioStream * ) s, 1 );
}

/** The stream is considered stopped before StartStream, or AFTER a call to Abort/StopStream (callback
 * returning !paContinue is not considered)
 *
 */
static PaError IsStreamStopped( PaStream *s )
{
    PaIoaudioStream *stream = (PaIoaudioStream *)s;

    /* callback_finished indicates we need to join callback thread (ie. in Abort/StopStream) */
    return !IsStreamActive( s ) && !stream->callback_finished;
}

static PaError IsStreamActive( PaStream *s )
{
    PaIoaudioStream *stream = (PaIoaudioStream*)s;
    return stream->isActive;
}

static PaTime GetStreamTime( PaStream *s )
{
//    PaIoaudioStream *stream = (PaIoaudioStream*)s;

//    ioaudio_snd_pcm_status_alloca( &status );

    /* TODO: what if we have both?  does it really matter? */

    /* TODO: if running in callback mode, this will mean
     * libasound routines are being called from multiple threads.
     * need to verify that libasound is thread-safe. */

    //ioaudio_snd_pcm_status_get_tstamp( status, &timestamp );
    //return timestamp.tv_sec + (PaTime)timestamp.tv_usec / 1e6;
    return PaUtil_GetTime();
}

static double GetStreamCpuLoad( PaStream* s )
{
    PaIoaudioStream *stream = (PaIoaudioStream*)s;

    return PaUtil_GetCpuLoad( &stream->cpuLoadMeasurer );
}

#if 0
static int SetApproximateSampleRate( snd_pcm_t *pcm, snd_pcm_channel_params_t *hwParams, double sampleRate )
{
    PaError result = paNoError;
    unsigned long approx = (unsigned long) sampleRate;
    int dir = 0;
    double fraction = sampleRate - approx;

    assert( pcm && hwParams );

    if( fraction > 0.0 )
    {
        if( fraction > 0.5 )
        {
            ++approx;
            dir = -1;
        }
        else
            dir = 1;
    }

    ioaudio_snd_pcm_channel_params( pcm, &params );
//    if( ioaudio_snd_pcm_hw_params_set_rate( pcm, hwParams, approx, dir ) < 0)
//        result = paInvalidSampleRate;

    return result;

#if 0
error:

    /* Log */
    {
        unsigned int _min = 0, _max = 0; int _dir = 0;
        ENSURE_( ioaudio_snd_pcm_hw_params_get_rate_min( hwParams, &_min, &_dir ), paUnanticipatedHostError );
        ENSURE_( ioaudio_snd_pcm_hw_params_get_rate_max( hwParams, &_max, &_dir ), paUnanticipatedHostError );
        PA_DEBUG(( "%s: SR min = %d, max = %d, req = %lu\n", __FUNCTION__, _min, _max, approx ));
    }

    goto end;
#endif
}
#endif

#if 0
/* Return exact sample rate in param sampleRate */
static int GetExactSampleRate( snd_pcm_channel_params_t *hwParams, double *sampleRate )
{
    int err = paNoError;
    unsigned int num, den;
    int err;

    assert( hwParams );

    err = ioaudio_snd_pcm_hw_params_get_rate_numden( hwParams, &num, &den );
    *sampleRate = (double) num / den;

    return err;
}
#endif

/* Utility functions for blocking/callback interfaces */

/* Atomic restart of stream (we don't want the intermediate state visible) */
static PaError IoaudioRestart( PaIoaudioStream *stream )
{
    PaError result = paNoError;

    PA_ENSURE( PaUnixMutex_Lock( &stream->stateMtx ) );
    PA_ENSURE( IoaudioStop( stream, 0 ) );
    PA_ENSURE( IoaudioStart( stream, 0 ) );

    PA_DEBUG(( "%s: Restarted audio\n", __FUNCTION__ ));

error:
    PA_ENSURE( PaUnixMutex_Unlock( &stream->stateMtx ) );

    return result;
}

/** Recover from xrun state.
 *
 */
static PaError PaIoaudioStream_HandleXrun( PaIoaudioStream *self )
{
    PaError result = paNoError;
    //snd_pcm_status_t *st;
    snd_pcm_channel_status_t st;
    PaTime now = PaUtil_GetTime();
    snd_timestamp_t t;
    int restartIoaudio = 0; /* do not restart Alsa by default */

    //ioaudio_snd_pcm_status_alloca( &st );

    if( self->playback.pcm )
    {
        ioaudio_snd_pcm_channel_status( self->playback.pcm, &st );
        if( SND_PCM_STATUS_UNDERRUN == st.status )
        {
            t = st.stop_time;
            self->underrun = now * 1000 - ((PaTime) t.tv_sec * 1000 + (PaTime) t.tv_usec / 1000);

            if (!self->playback.canMmap)
            {
                PA_DEBUG(( "%s: [playback] non-MMAP-PCM failed recovering from XRUN, will restart io-audio\n", __FUNCTION__ ));
                ++ restartIoaudio; /* did not manage to recover */
            }
            else
                ++ restartIoaudio; /* always restart MMAPed device */
        }
    }
    if( self->capture.pcm )
    {
        ioaudio_snd_pcm_channel_status( self->capture.pcm, &st );
        if( SND_PCM_STATUS_OVERRUN == st.status )
        {
            t = st.stop_time;
            self->overrun = now * 1000 - ((PaTime) t.tv_sec * 1000 + (PaTime) t.tv_usec / 1000);

            if (!self->capture.canMmap)
            {
                PA_DEBUG(( "%s: [capture] non-MMAP-PCM failed recovering from XRUN, will restart io-audio\n", __FUNCTION__ ));
                ++ restartIoaudio; /* did not manage to recover */
            }
            else
                ++ restartIoaudio; /* always restart MMAPed device */
        }
    }

    if( restartIoaudio )
    {
        PA_DEBUG(( "%s: restarting Ioaudio to recover from XRUN\n", __FUNCTION__ ));
        PA_ENSURE( IoaudioRestart( self ) );
    }

end:
    return result;
error:
    goto end;
}

/** Decide if we should continue polling for specified direction, eventually adjust the poll timeout.
 *
 */
static PaError ContinuePoll( const PaIoaudioStream *stream, StreamDirection streamDir, int *pollTimeout, int *continuePoll )
{
    PaError result = paNoError;
    snd_pcm_sframes_t delay, margin;
    int err;
    //const PaIoaudioStreamComponent *component = NULL, *otherComponent = NULL;
    const PaIoaudioStreamComponent *otherComponent = NULL;

    *continuePoll = 1;

    if( StreamDirection_In == streamDir )
    {
        //component = &stream->capture;
        otherComponent = &stream->playback;
    }
    else
    {
        //component = &stream->playback;
        otherComponent = &stream->capture;
    }

    if( (err = ioaudio_snd_pcm_delay( otherComponent->pcm, &delay )) < 0 )
    {
        if( err == -EPIPE )
        {
            /* Xrun */
            *continuePoll = 0;
            goto error;
        }

        ENSURE_( err, paUnanticipatedHostError );
    }

    if( StreamDirection_Out == streamDir )
    {
        /* Number of eligible frames before capture overrun */
        delay = otherComponent->bufferSize - delay;
    }
    margin = delay - otherComponent->framesPerBuffer / 2;

    if( margin < 0 )
    {
        PA_DEBUG(( "%s: Stopping poll for %s\n", __FUNCTION__, StreamDirection_In == streamDir ? "capture" : "playback" ));
        *continuePoll = 0;
    }
    else if( margin < otherComponent->framesPerBuffer )
    {
        *pollTimeout = CalculatePollTimeout( stream, margin );
        PA_DEBUG(( "%s: Trying to poll again for %s frames, pollTimeout: %d\n",
                    __FUNCTION__, StreamDirection_In == streamDir ? "capture" : "playback", *pollTimeout ));
    }

error:
    return result;
}

/* Callback interface */

static void OnExit( void *data )
{
    PaIoaudioStream *stream = (PaIoaudioStream *) data;

    assert( data );

    PaUtil_ResetCpuLoadMeasurer( &stream->cpuLoadMeasurer );

    stream->callback_finished = 1;  /* Let the outside world know stream was stopped in callback */
    PA_DEBUG(( "%s: Stopping IOAUDIO handles\n", __FUNCTION__ ));
    IoaudioStop( stream, stream->callbackAbort );

    PA_DEBUG(( "%s: Stoppage\n", __FUNCTION__ ));

    /* Eventually notify user all buffers have played */
    if( stream->streamRepresentation.streamFinishedCallback )
    {
        stream->streamRepresentation.streamFinishedCallback( stream->streamRepresentation.userData );
    }
    stream->isActive = 0;
}

static void CalculateTimeInfo( PaIoaudioStream *stream, PaStreamCallbackTimeInfo *timeInfo )
{
#if 1
    PA_DEBUG(( "%s disabled\n", __func__ ));
    timeInfo->currentTime = PaUtil_GetTime();
    if( stream->capture.pcm )
    {
        timeInfo->inputBufferAdcTime = timeInfo->currentTime;
    }
    if( stream->playback.pcm )
    {
        timeInfo->outputBufferDacTime = timeInfo->currentTime;
    }
#else
    snd_pcm_channel_status_t *capture_status, *playback_status;
    snd_timestamp_t capture_timestamp, playback_timestamp;
    PaTime capture_time = 0., playback_time = 0.;

    ioaudio_snd_pcm_status_alloca( &capture_status );
    ioaudio_snd_pcm_status_alloca( &playback_status );

    if( stream->capture.pcm )
    {
        snd_pcm_sframes_t capture_delay;

        ioaudio_snd_pcm_status( stream->capture.pcm, capture_status );
        ioaudio_snd_pcm_status_get_tstamp( capture_status, &capture_timestamp );

        capture_time = capture_timestamp.tv_sec +
            ((PaTime)capture_timestamp.tv_usec / 1000000.0);
        timeInfo->currentTime = capture_time;

        capture_delay = ioaudio_snd_pcm_status_get_delay( capture_status );
        timeInfo->inputBufferAdcTime = timeInfo->currentTime -
            (PaTime)capture_delay / stream->streamRepresentation.streamInfo.sampleRate;
    }
    if( stream->playback.pcm )
    {
        snd_pcm_sframes_t playback_delay;

        ioaudio_snd_pcm_status( stream->playback.pcm, playback_status );
        ioaudio_snd_pcm_status_get_tstamp( playback_status, &playback_timestamp );

        playback_time = playback_timestamp.tv_sec +
            ((PaTime)playback_timestamp.tv_usec / 1000000.0);

        if( stream->capture.pcm ) /* Full duplex */
        {
            /* Hmm, we have both a playback and a capture timestamp.
             * Hopefully they are the same... */
            if( fabs( capture_time - playback_time ) > 0.01 )
                PA_DEBUG(("Capture time and playback time differ by %f\n", fabs(capture_time-playback_time)));
        }
        else
            timeInfo->currentTime = playback_time;

        playback_delay = ioaudio_snd_pcm_status_get_delay( playback_status );
        timeInfo->outputBufferDacTime = timeInfo->currentTime +
            (PaTime)playback_delay / stream->streamRepresentation.streamInfo.sampleRate;
    }
#endif
}

/** Called after buffer processing is finished.
 *
 * A number of mmapped frames is committed, it is possible that an xrun has occurred in the meantime.
 *
 * @param numFrames The number of frames that has been processed
 * @param xrun Return whether an xrun has occurred
 */
static PaError PaIoaudioStreamComponent_EndProcessing( PaIoaudioStreamComponent *self, unsigned long numFrames, int *xrun )
{
    PaError result = paNoError;
    int res = 0;

    /* @concern FullDuplex It is possible that only one direction is marked ready after polling, and processed
     * afterwards
     */
    if( !self->ready )
        goto end;

    if( !self->canMmap && StreamDirection_Out == self->streamDir )
    {
        /* Play sound */
        if( self->hostInterleaved )
            res = ioaudio_snd_pcm_write( self->pcm, self->nonMmapBuffer, numFrames );
        else
        {
            void *bufs[self->numHostChannels];
            int bufsize = ioaudio_snd_pcm_format_size( self->nativeFormat, self->framesPerBuffer + 1 );
            unsigned char *buffer = self->nonMmapBuffer;
            int i;
            for( i = 0; i < self->numHostChannels; ++i )
            {
                bufs[i] = buffer;
                buffer += bufsize;
            }
            res = ioaudio_snd_pcm_write( self->pcm, bufs, numFrames );
        }
    }

#if 0
    if( self->canMmap )
        res = ioaudio_snd_pcm_mmap_commit( self->pcm, self->offset, numFrames );
    else
    {
        /* using realloc for optimisation
        free( self->nonMmapBuffer );
        self->nonMmapBuffer = NULL;
        */
    }
#endif

    if( res == -EPIPE || res == -ESTRPIPE )
    {
        *xrun = 1;
    }
    else
    {
        ENSURE_( res, paUnanticipatedHostError );
    }

end:
error:
    return result;
}

#if 0
/* Extract buffer from channel area */
static unsigned char *ExtractAddress( const snd_pcm_channel_area_t *area, snd_pcm_uframes_t offset )
{
    return (unsigned char *) area->addr + (area->first + offset * area->step) / 8;
}
#endif

/** Do necessary adaption between user and host channels.
 *
    @concern ChannelAdaption Adapting between user and host channels can involve silencing unused channels and
    duplicating mono information if host outputs come in pairs.
 */
static PaError PaIoaudioStreamComponent_DoChannelAdaption( PaIoaudioStreamComponent *self, PaUtilBufferProcessor *bp, int numFrames )
{
    PaError result = paNoError;
    PA_DEBUG(( "%s disabled\n", __func__ ));
#if 0
    unsigned char *p;
    int i;
    int unusedChans = self->numHostChannels - self->numUserChannels;
    unsigned char *src, *dst;
    int convertMono = (self->numHostChannels % 2) == 0 && (self->numUserChannels % 2) != 0;

    assert( StreamDirection_Out == self->streamDir );

    if( self->hostInterleaved )
    {
        int swidth = ioaudio_snd_pcm_format_size( self->nativeFormat, 1 );
        unsigned char *buffer = self->canMmap ? ExtractAddress( self->channelAreas, self->offset ) : self->nonMmapBuffer;

        /* Start after the last user channel */
        p = buffer + self->numUserChannels * swidth;

        if( convertMono )
        {
            /* Convert the last user channel into stereo pair */
            src = buffer + (self->numUserChannels - 1) * swidth;
            for( i = 0; i < numFrames; ++i )
            {
                dst = src + swidth;
                memcpy( dst, src, swidth );
                src += self->numHostChannels * swidth;
            }

            /* Don't touch the channel we just wrote to */
            p += swidth;
            --unusedChans;
        }

        if( unusedChans > 0 )
        {
            /* Silence unused output channels */
            for( i = 0; i < numFrames; ++i )
            {
                memset( p, 0, swidth * unusedChans );
                p += self->numHostChannels * swidth;
            }
        }
    }
    else
    {
        /* We extract the last user channel */
        if( convertMono )
        {
            ENSURE_( ioaudio_snd_pcm_area_copy( self->channelAreas + self->numUserChannels, self->offset, self->channelAreas +
                    (self->numUserChannels - 1), self->offset, numFrames, self->nativeFormat ), paUnanticipatedHostError );
            --unusedChans;
        }
        if( unusedChans > 0 )
        {
            ioaudio_snd_pcm_areas_silence( self->channelAreas + (self->numHostChannels - unusedChans), self->offset, unusedChans, numFrames,
                    self->nativeFormat );
        }
    }

error:
#endif
    return result;
}

static PaError PaIoaudioStream_EndProcessing( PaIoaudioStream *self, unsigned long numFrames, int *xrunOccurred )
{
    PaError result = paNoError;
    int xrun = 0;

    if( self->capture.pcm )
    {
        PA_ENSURE( PaIoaudioStreamComponent_EndProcessing( &self->capture, numFrames, &xrun ) );
    }
    if( self->playback.pcm )
    {
        if( self->playback.numHostChannels > self->playback.numUserChannels )
        {
            PA_ENSURE( PaIoaudioStreamComponent_DoChannelAdaption( &self->playback, &self->bufferProcessor, numFrames ) );
        }
        PA_ENSURE( PaIoaudioStreamComponent_EndProcessing( &self->playback, numFrames, &xrun ) );
    }

error:
    *xrunOccurred = xrun;
    return result;
}

/** Update the number of available frames.
 *
 */
static PaError PaIoaudioStreamComponent_GetAvailableFrames( PaIoaudioStreamComponent *self, unsigned long *numFrames, int *xrunOccurred )
{
    PaError result = paNoError;
    snd_pcm_channel_status_t status;
    snd_pcm_uframes_t framesAvail;

    status.channel = ConvertStreamDirToChannelDir( self->streamDir );
    ioaudio_snd_pcm_channel_status( self->pcm, &status );
    framesAvail = (snd_pcm_uframes_t)status.free;

    *xrunOccurred = 0;

    if( -EPIPE == framesAvail )
    {
        *xrunOccurred = 1;
        framesAvail = 0;
    }
    else
    {
        ENSURE_( framesAvail, paUnanticipatedHostError );
    }

    *numFrames = framesAvail;

error:
    return result;
}

/** Fill in pollfd objects.
 */
static PaError PaIoaudioStreamComponent_BeginPolling( PaIoaudioStreamComponent* self, struct pollfd* pfds )
{
    PaError result = paNoError;
    int i;
    for( i = 0; i < self->nfds; i++ )
    {
        pfds[ i ].fd = snd_pcm_file_descriptor
            (
            self->pcm,
            ConvertStreamDirToChannelDir( self->streamDir )
            );
        assert( 0 <= pfds[ i ].fd );
    }

    self->ready = 0;

    return result;
}

/** Examine results from poll().
 *
 * @param pfds pollfds to inspect
 * @param shouldPoll Should we continue to poll
 * @param xrun Has an xrun occurred
 */
static PaError PaIoaudioStreamComponent_EndPolling( PaIoaudioStreamComponent* self, struct pollfd* pfds, int* shouldPoll, int* xrun )
{
    PaError result = paNoError;
    unsigned short revents;
    int i;

    for( i = 0; i < self->nfds; i++ )
    {
        revents = pfds[ i ].revents;
        if( revents != 0 )
        {
            if( revents & POLLERR )
            {
                *xrun = 1;
            }
            else
            if( revents & POLLHUP )
            {
                *xrun = 1;
                PA_DEBUG(( "%s: revents has POLLHUP, processing as XRUN\n", __FUNCTION__ ));
            }
            else
                self->ready = 1;

            *shouldPoll = 0;
        }
    }

    return result;
}

/** Return the number of available frames for this stream.
 *
 * @concern FullDuplex The minimum available for the two directions is calculated, it might be desirable to ignore
 * one direction however (not marked ready from poll), so this is controlled by queryCapture and queryPlayback.
 *
 * @param queryCapture Check available for capture
 * @param queryPlayback Check available for playback
 * @param available The returned number of frames
 * @param xrunOccurred Return whether an xrun has occurred
 */
static PaError PaIoaudioStream_GetAvailableFrames( PaIoaudioStream *self, int queryCapture, int queryPlayback, unsigned long
        *available, int *xrunOccurred )
{
    PaError result = paNoError;
    unsigned long captureFrames, playbackFrames;
    *xrunOccurred = 0;

    assert( queryCapture || queryPlayback );

    if( queryCapture )
    {
        assert( self->capture.pcm );
        PA_ENSURE( PaIoaudioStreamComponent_GetAvailableFrames( &self->capture, &captureFrames, xrunOccurred ) );
        if( *xrunOccurred )
        {
            goto end;
        }
    }
    if( queryPlayback )
    {
        assert( self->playback.pcm );
        PA_ENSURE( PaIoaudioStreamComponent_GetAvailableFrames( &self->playback, &playbackFrames, xrunOccurred ) );
        if( *xrunOccurred )
        {
            goto end;
        }
    }

    if( queryCapture && queryPlayback )
    {
        *available = PA_MIN( captureFrames, playbackFrames );
        /*PA_DEBUG(("capture: %lu, playback: %lu, combined: %lu\n", captureFrames, playbackFrames, *available));*/
    }
    else if( queryCapture )
    {
        *available = captureFrames;
    }
    else
    {
        *available = playbackFrames;
    }

end:
error:
    return result;
}

/** Wait for and report available buffer space from IOAUDIO.
 *
 * Unless IOAUDIO reports a minimum of frames available for I/O, we poll the IOAUDIO filedescriptors for more.
 * Both of these operations can uncover xrun conditions.
 *
 * @concern Xruns Both polling and querying available frames can report an xrun condition.
 *
 * @param framesAvail Return the number of available frames
 * @param xrunOccurred Return whether an xrun has occurred
 */
static PaError PaIoaudioStream_WaitForFrames( PaIoaudioStream *self, unsigned long *framesAvail, int *xrunOccurred )
{
    PaError result = paNoError;
    int pollPlayback = self->playback.pcm != NULL, pollCapture = self->capture.pcm != NULL;
    int pollTimeout = self->pollTimeout;
    int xrun = 0, timeouts = 0;
    int pollResults;

    assert( self );
    assert( framesAvail );

    if( !self->callbackMode )
    {
        /* In blocking mode we will only wait if necessary */
        PA_ENSURE( PaIoaudioStream_GetAvailableFrames( self, self->capture.pcm != NULL, self->playback.pcm != NULL,
                    framesAvail, &xrun ) );
        if( xrun )
        {
            goto end;
        }

        if( *framesAvail > 0 )
        {
            /* Mark pcms ready from poll */
            if( self->capture.pcm )
                self->capture.ready = 1;
            if( self->playback.pcm )
                self->playback.ready = 1;

            goto end;
        }
    }

    while( pollPlayback || pollCapture )
    {
        int totalFds = 0;
        struct pollfd *capturePfds = NULL, *playbackPfds = NULL;

#ifdef PTHREAD_CANCELED
        pthread_testcancel();
#endif
        if( pollCapture )
        {
            capturePfds = self->pfds;
            PA_ENSURE( PaIoaudioStreamComponent_BeginPolling( &self->capture, capturePfds ) );
            totalFds += self->capture.nfds;
        }
        if( pollPlayback )
        {
            playbackPfds = self->pfds + (self->capture.pcm ? self->capture.nfds : 0);
            PA_ENSURE( PaIoaudioStreamComponent_BeginPolling( &self->playback, playbackPfds ) );
            totalFds += self->playback.nfds;
        }

        pollResults = poll( self->pfds, totalFds, pollTimeout );

        if( pollResults < 0 )
        {
            /*  XXX: Depend on preprocessor condition? */
            if( errno == EINTR )
            {
                /* gdb */
                Pa_Sleep( 1 ); /* avoid hot loop */
                continue;
            }

            /* TODO: Add macro for checking system calls */
            PA_ENSURE( paInternalError );
        }
        else
        if (pollResults == 0)
        {

           /* Suspended, paused or failed device can provide 0 poll results. To avoid deadloop in such situation
            * we simply run counter 'timeouts' which detects 0 poll result and accumulates. As soon as 2048 timouts (around 2 seconds)
            * are achieved we simply fail function with paTimedOut to notify waiting methods that device is not capable
            * of providing audio data anymore and needs some corresponding recovery action.
            * Note that 'timeouts' is reset to 0 if poll() managed to return non 0 results.
            */

            /*PA_DEBUG(( "%s: poll == 0 results, timed out, %d times left\n", __FUNCTION__, 2048 - timeouts ));*/

            ++ timeouts;
            if (timeouts > 1) /* sometimes device times out, but normally once, so we do not sleep any time */
            {
                Pa_Sleep( 1 ); /* avoid hot loop */
            }
            /* not else ! */
            if (timeouts >= 2048) /* audio device not working, shall return error to notify waiters */
            {
				*framesAvail = 0; /* no frames available for processing */
				xrun = 1; /* try recovering device */

                PA_DEBUG(( "%s: poll timed out\n", __FUNCTION__, timeouts ));
                goto end;/*PA_ENSURE( paTimedOut );*/
            }
        }
        else
        if (pollResults > 0)
        {
            /* reset timouts counter */
            timeouts = 0;

            /* check the return status of our pfds */
            if( pollCapture )
            {
                PA_ENSURE( PaIoaudioStreamComponent_EndPolling( &self->capture, capturePfds, &pollCapture, &xrun ) );
            }
            if( pollPlayback )
            {
                PA_ENSURE( PaIoaudioStreamComponent_EndPolling( &self->playback, playbackPfds, &pollPlayback, &xrun ) );
            }
            if( xrun )
            {
                break;
            }
        }

        /* @concern FullDuplex If only one of two pcms is ready we may want to compromise between the two.
         * If there is less than half a period's worth of samples left of frames in the other pcm's buffer we will
         * stop polling.
         */
        if( self->capture.pcm && self->playback.pcm )
        {
            if( pollCapture && !pollPlayback )
            {
                PA_ENSURE( ContinuePoll( self, StreamDirection_In, &pollTimeout, &pollCapture ) );
            }
            else if( pollPlayback && !pollCapture )
            {
                PA_ENSURE( ContinuePoll( self, StreamDirection_Out, &pollTimeout, &pollPlayback ) );
            }
        }
    }

    if( !xrun )
    {
        /* Get the number of available frames for the pcms that are marked ready.
         * @concern FullDuplex If only one direction is marked ready (from poll), the number of frames available for
         * the other direction is returned. Output is normally preferred over capture however, so capture frames may be
         * discarded to avoid overrun unless paNeverDropInput is specified.
         */
        int captureReady = self->capture.pcm ? self->capture.ready : 0,
            playbackReady = self->playback.pcm ? self->playback.ready : 0;
        PA_ENSURE( PaIoaudioStream_GetAvailableFrames( self, captureReady, playbackReady, framesAvail, &xrun ) );

        if( self->capture.pcm && self->playback.pcm )
        {
            if( !self->playback.ready && !self->neverDropInput )
            {
                /* Drop input, a period's worth */
                assert( self->capture.ready );
                PaIoaudioStreamComponent_EndProcessing( &self->capture, PA_MIN( self->capture.framesPerBuffer,
                            *framesAvail ), &xrun );
                *framesAvail = 0;
                self->capture.ready = 0;
            }
        }
        else if( self->capture.pcm )
            assert( self->capture.ready );
        else
            assert( self->playback.ready );
    }

end:
error:
    if( xrun )
    {
        /* Recover from the xrun state */
        PA_ENSURE( PaIoaudioStream_HandleXrun( self ) );
        *framesAvail = 0;
    }
    else
    {
        if( 0 != *framesAvail )
        {
            /* If we're reporting frames eligible for processing, one of the handles better be ready */
            PA_UNLESS( self->capture.ready || self->playback.ready, paInternalError );
        }
    }
    *xrunOccurred = xrun;

    return result;
}

/** Register per-channel IOAUDIO buffer information with buffer processor.
 *
 * Mmapped buffer space is acquired from IOAUDIO, and registered with the buffer processor. Differences between the
 * number of host and user channels is taken into account.
 *
 * @param numFrames On entrance the number of requested frames, on exit the number of contiguously accessible frames.
 */
static PaError PaIoaudioStreamComponent_RegisterChannels( PaIoaudioStreamComponent* self, PaUtilBufferProcessor* bp,
        unsigned long* numFrames, int* xrun )
{
    PaError result = paNoError;
//    const snd_pcm_channel_area_t *areas, *area;
    void (*setChannel)(PaUtilBufferProcessor *, unsigned int, void *, unsigned int) =
        StreamDirection_In == self->streamDir ? PaUtil_SetInputChannel : PaUtil_SetOutputChannel;
    unsigned char *buffer, *p;
    int i;
    unsigned long framesAvail;

    /* This _must_ be called before mmap_begin */
    PA_ENSURE( PaIoaudioStreamComponent_GetAvailableFrames( self, &framesAvail, xrun ) );
    if( *xrun )
    {
        *numFrames = 0;
        goto end;
    }

    if( self->canMmap )
    {
        //ENSURE_( ioaudio_snd_pcm_mmap_begin( self->pcm, &areas, &self->offset, numFrames ), paUnanticipatedHostError );
        /* @concern ChannelAdaption Buffer address is recorded so we can do some channel adaption later */
        //self->channelAreas = (snd_pcm_channel_area_t *)areas;
    }
    else
    {
        unsigned int bufferSize = self->numHostChannels * ioaudio_snd_pcm_format_size( self->nativeFormat, *numFrames );
        if (bufferSize > self->nonMmapBufferSize)
        {
            self->nonMmapBuffer = realloc(self->nonMmapBuffer, (self->nonMmapBufferSize = bufferSize));
            if (!self->nonMmapBuffer)
            {
                result = paInsufficientMemory;
                goto error;
            }
        }
    }

    if( self->hostInterleaved )
    {
        int swidth = ioaudio_snd_pcm_format_size( self->nativeFormat, 1 );

//        p = buffer = self->canMmap ? ExtractAddress( areas, self->offset ) : self->nonMmapBuffer;
        p = buffer = self->nonMmapBuffer;
        for( i = 0; i < self->numUserChannels; ++i )
        {
            /* We're setting the channels up to userChannels, but the stride will be hostChannels samples */
            setChannel( bp, i, p, self->numHostChannels );
            p += swidth;
        }
    }
    else
    {
#if 0
        if( self->canMmap )
        {
            for( i = 0; i < self->numUserChannels; ++i )
            {
                area = areas + i;
                buffer = ExtractAddress( area, self->offset );
                setChannel( bp, i, buffer, 1 );
            }
        }
        else
#endif
        {
            unsigned int buf_per_ch_size = self->nonMmapBufferSize / self->numHostChannels;
            buffer = self->nonMmapBuffer;
            for( i = 0; i < self->numUserChannels; ++i )
            {
                setChannel( bp, i, buffer, 1 );
                buffer += buf_per_ch_size;
            }
        }
    }

    if( !self->canMmap && StreamDirection_In == self->streamDir )
    {
        /* Read sound */
        int res;
        if( self->hostInterleaved )
            res = ioaudio_snd_pcm_read( self->pcm, self->nonMmapBuffer, *numFrames );
        else
        {
            void *bufs[self->numHostChannels];
            unsigned int buf_per_ch_size = self->nonMmapBufferSize / self->numHostChannels;
            unsigned char *buffer = self->nonMmapBuffer;
            int i;
            for( i = 0; i < self->numHostChannels; ++i )
            {
                bufs[i] = buffer;
                buffer += buf_per_ch_size;
            }
            res = ioaudio_snd_pcm_read( self->pcm, bufs, *numFrames );
        }
        if( res == -EPIPE || res == -ESTRPIPE )
        {
            *xrun = 1;
            *numFrames = 0;
        }
    }

end:
error:
    return result;
}

/** Initiate buffer processing.
 *
 * IOAUDIO buffers are registered with the PA buffer processor and the buffer size (in frames) set.
 *
 * @concern FullDuplex If both directions are being processed, the minimum amount of frames for the two directions is
 * calculated.
 *
 * @param numFrames On entrance the number of available frames, on exit the number of received frames
 * @param xrunOccurred Return whether an xrun has occurred
 */
static PaError PaIoaudioStream_SetUpBuffers( PaIoaudioStream* self, unsigned long* numFrames, int* xrunOccurred )
{
    PaError result = paNoError;
    unsigned long captureFrames = ULONG_MAX, playbackFrames = ULONG_MAX, commonFrames = 0;
    int xrun = 0;

    if( *xrunOccurred )
    {
        *numFrames = 0;
        return result;
    }
    /* If we got here at least one of the pcm's should be marked ready */
    PA_UNLESS( self->capture.ready || self->playback.ready, paInternalError );

    /* Extract per-channel IOAUDIO buffer pointers and register them with the buffer processor.
     * It is possible that a direction is not marked ready however, because it is out of sync with the other.
     */
    if( self->capture.pcm && self->capture.ready )
    {
        captureFrames = *numFrames;
        PA_ENSURE( PaIoaudioStreamComponent_RegisterChannels( &self->capture, &self->bufferProcessor, &captureFrames,
                    &xrun ) );
    }
    if( self->playback.pcm && self->playback.ready )
    {
        playbackFrames = *numFrames;
        PA_ENSURE( PaIoaudioStreamComponent_RegisterChannels( &self->playback, &self->bufferProcessor, &playbackFrames,
                    &xrun ) );
    }
    if( xrun )
    {
        /* Nothing more to do */
        assert( 0 == commonFrames );
        goto end;
    }

    commonFrames = PA_MIN( captureFrames, playbackFrames );
    /* assert( commonFrames <= *numFrames ); */
    if( commonFrames > *numFrames )
    {
        /* Hmmm ... how come there are more frames available than we requested!? Blah. */
        PA_DEBUG(( "%s: Common available frames are reported to be more than number requested: %lu, %lu, callbackMode: %d\n", __FUNCTION__,
                    commonFrames, *numFrames, self->callbackMode ));
        if( self->capture.pcm )
        {
            PA_DEBUG(( "%s: captureFrames: %lu, capture.ready: %d\n", __FUNCTION__, captureFrames, self->capture.ready ));
        }
        if( self->playback.pcm )
        {
            PA_DEBUG(( "%s: playbackFrames: %lu, playback.ready: %d\n", __FUNCTION__, playbackFrames, self->playback.ready ));
        }

        commonFrames = 0;
        goto end;
    }

    /* Inform PortAudio of the number of frames we got.
     * @concern FullDuplex We might be experiencing underflow in either end; if its an input underflow, we go on
     * with output. If its output underflow however, depending on the paNeverDropInput flag, we may want to simply
     * discard the excess input or call the callback with paOutputOverflow flagged.
     */
    if( self->capture.pcm )
    {
        if( self->capture.ready )
        {
            PaUtil_SetInputFrameCount( &self->bufferProcessor, commonFrames );
        }
        else
        {
            /* We have input underflow */
            PaUtil_SetNoInput( &self->bufferProcessor );
        }
    }
    if( self->playback.pcm )
    {
        if( self->playback.ready )
        {
            PaUtil_SetOutputFrameCount( &self->bufferProcessor, commonFrames );
        }
        else
        {
            /* We have output underflow, but keeping input data (paNeverDropInput) */
            assert( self->neverDropInput );
            assert( self->capture.pcm != NULL );
            PA_DEBUG(( "%s: Setting output buffers to NULL\n", __FUNCTION__ ));
            PaUtil_SetNoOutput( &self->bufferProcessor );
        }
    }

end:
    *numFrames = commonFrames;
error:
    if( xrun )
    {
        PA_ENSURE( PaIoaudioStream_HandleXrun( self ) );
        *numFrames = 0;
    }
    *xrunOccurred = xrun;

    return result;
}

/** Callback thread's function.
 *
 * Roughly, the workflow can be described in the following way: The number of available frames that can be processed
 * directly is obtained from IOAUDIO, we then request as much directly accessible memory as possible within this amount
 * from IOAUDIO. The buffer memory is registered with the PA buffer processor and processing is carried out with
 * PaUtil_EndBufferProcessing. Finally, the number of processed frames is reported to IOAUDIO. The processing can
 * happen in several iterations untill we have consumed the known number of available frames (or an xrun is detected).
 */
static void *CallbackThreadFunc( void *userData )
{
    PaError result = paNoError;
    PaIoaudioStream *stream = (PaIoaudioStream*) userData;
    PaStreamCallbackTimeInfo timeInfo = {0, 0, 0};
    snd_pcm_sframes_t startThreshold = 0;
    int callbackResult = paContinue;
    PaStreamCallbackFlags cbFlags = 0;  /* We might want to keep state across iterations */
    //int streamStarted = 0;
    snd_pcm_channel_status_t status;

    assert( stream );

    /* Execute OnExit when exiting */
    pthread_cleanup_push( &OnExit, stream );

    /* Not implemented */
    assert( !stream->primeBuffers );

    /* @concern StreamStart If the output is being primed the output pcm needs to be prepared, otherwise the
     * stream is started immediately. The latter involves signaling the waiting main thread.
     */
    if( stream->primeBuffers )
    {
        snd_pcm_sframes_t avail;

        if( stream->playback.pcm )
            ENSURE_( ioaudio_snd_pcm_channel_prepare( stream->playback.pcm, SND_PCM_CHANNEL_PLAYBACK ), paUnanticipatedHostError );
        if( stream->capture.pcm && !stream->pcmsSynced )
            ENSURE_( ioaudio_snd_pcm_channel_prepare( stream->capture.pcm, SND_PCM_CHANNEL_CAPTURE ), paUnanticipatedHostError );

        /* We can't be certain that the whole ring buffer is available for priming, but there should be
         * at least one period */
        status.channel = SND_PCM_CHANNEL_PLAYBACK;
        ioaudio_snd_pcm_channel_status( stream->playback.pcm, &status );
        avail = (snd_pcm_uframes_t)status.free;
        startThreshold = avail - (avail % stream->playback.framesPerBuffer);
        assert( startThreshold >= stream->playback.framesPerBuffer );
    }
    else
    {
        PA_ENSURE( PaUnixThread_PrepareNotify( &stream->thread ) );
        /* Buffer will be zeroed */
        PA_ENSURE( IoaudioStart( stream, 0 ) );
        PA_ENSURE( PaUnixThread_NotifyParent( &stream->thread ) );

        //streamStarted = 1;
    }

    while( 1 )
    {
        unsigned long framesAvail, framesGot;
        int xrun = 0;

#ifdef PTHREAD_CANCELED
		pthread_testcancel();
#endif

        /* @concern StreamStop if the main thread has requested a stop and the stream has not been effectively
         * stopped we signal this condition by modifying callbackResult (we'll want to flush buffered output).
         */
        if( PaUnixThread_StopRequested( &stream->thread ) && paContinue == callbackResult )
        {
            PA_DEBUG(( "Setting callbackResult to paComplete\n" ));
            callbackResult = paComplete;
        }

        if( paContinue != callbackResult )
        {
            stream->callbackAbort = (paAbort == callbackResult);
            if( stream->callbackAbort ||
                    /** @concern BlockAdaption: Go on if adaption buffers are empty */
                    PaUtil_IsBufferProcessorOutputEmpty( &stream->bufferProcessor ) )
            {
                goto end;
            }

            PA_DEBUG(( "%s: Flushing buffer processor\n", __FUNCTION__ ));
            /* There is still buffered output that needs to be processed */
        }

        /* Wait for data to become available, this comes down to polling the IOAUDIO file descriptors untill we have
         * a number of available frames.
         */
        PA_ENSURE( PaIoaudioStream_WaitForFrames( stream, &framesAvail, &xrun ) );
        if( xrun )
        {
            assert( 0 == framesAvail );
            continue;

            /* XXX: Report xruns to the user? A situation is conceivable where the callback is never invoked due
             * to constant xruns, it might be desirable to notify the user of this.
             */
        }

        /* Consume buffer space. Once we have a number of frames available for consumption we must retrieve the
         * mmapped buffers from IOAUDIO, this is contiguously accessible memory however, so we may receive smaller
         * portions at a time than is available as a whole. Therefore we should be prepared to process several
         * chunks successively. The buffers are passed to the PA buffer processor.
         */
        while( framesAvail > 0 )
        {
            xrun = 0;

#ifdef PTHREAD_CANCELED
           pthread_testcancel();
#endif

            /** @concern Xruns Under/overflows are to be reported to the callback */
            if( stream->underrun > 0.0 )
            {
                cbFlags |= paOutputUnderflow;
                stream->underrun = 0.0;
            }
            if( stream->overrun > 0.0 )
            {
                cbFlags |= paInputOverflow;
                stream->overrun = 0.0;
            }
            if( stream->capture.pcm && stream->playback.pcm )
            {
                /** @concern FullDuplex It's possible that only one direction is being processed to avoid an
                 * under- or overflow, this should be reported correspondingly */
                if( !stream->capture.ready )
                {
                    cbFlags |= paInputUnderflow;
                    PA_DEBUG(( "%s: Input underflow\n", __FUNCTION__ ));
                }
                else if( !stream->playback.ready )
                {
                    cbFlags |= paOutputOverflow;
                    PA_DEBUG(( "%s: Output overflow\n", __FUNCTION__ ));
                }
            }

#if 0
            CallbackUpdate( &stream->threading );
#endif
            CalculateTimeInfo( stream, &timeInfo );
            PaUtil_BeginBufferProcessing( &stream->bufferProcessor, &timeInfo, cbFlags );
            cbFlags = 0;

            /* CPU load measurement should include processing activivity external to the stream callback */
            PaUtil_BeginCpuLoadMeasurement( &stream->cpuLoadMeasurer );

            framesGot = framesAvail;
            if( paUtilFixedHostBufferSize == stream->bufferProcessor.hostBufferSizeMode )
            {
                /* We've committed to a fixed host buffer size, stick to that */
                framesGot = framesGot >= stream->maxFramesPerHostBuffer ? stream->maxFramesPerHostBuffer : 0;
            }
            else
            {
                /* We've committed to an upper bound on the size of host buffers */
                assert( paUtilBoundedHostBufferSize == stream->bufferProcessor.hostBufferSizeMode );
                framesGot = PA_MIN( framesGot, stream->maxFramesPerHostBuffer );
            }
            PA_ENSURE( PaIoaudioStream_SetUpBuffers( stream, &framesGot, &xrun ) );
            /* Check the host buffer size against the buffer processor configuration */
            framesAvail -= framesGot;

            if( framesGot > 0 )
            {
                assert( !xrun );
                PaUtil_EndBufferProcessing( &stream->bufferProcessor, &callbackResult );
                PA_ENSURE( PaIoaudioStream_EndProcessing( stream, framesGot, &xrun ) );
            }
            PaUtil_EndCpuLoadMeasurement( &stream->cpuLoadMeasurer, framesGot );

            if( 0 == framesGot )
            {
                /* Go back to polling for more frames */
                break;

            }

            if( paContinue != callbackResult )
                break;
        }
    }

end:
    ; /* Hack to fix "label at end of compound statement" error caused by pthread_cleanup_pop(1) macro. */
    /* Match pthread_cleanup_push */
    pthread_cleanup_pop( 1 );

    PA_DEBUG(( "%s: Thread %d exiting\n ", __FUNCTION__, pthread_self() ));
    PaUnixThreading_EXIT( result );

error:
    PA_DEBUG(( "%s: Thread %d is canceled due to error %d\n ", __FUNCTION__, pthread_self(), result ));
    goto end;
}

/* Blocking interface */

static PaError ReadStream( PaStream* s, void *buffer, unsigned long frames )
{
    PaError result = paNoError;
    PaIoaudioStream *stream = (PaIoaudioStream*)s;
    unsigned long framesGot, framesAvail;
    void *userBuffer;
    snd_pcm_t *save = stream->playback.pcm;

    assert( stream );

    PA_UNLESS( stream->capture.pcm, paCanNotReadFromAnOutputOnlyStream );

    /* Disregard playback */
    stream->playback.pcm = NULL;

    if( stream->overrun > 0. )
    {
        result = paInputOverflowed;
        stream->overrun = 0.0;
    }

    if( stream->capture.userInterleaved )
    {
        userBuffer = buffer;
    }
    else
    {
        /* Copy channels into local array */
        userBuffer = stream->capture.userBuffers;
        memcpy( userBuffer, buffer, sizeof (void *) * stream->capture.numUserChannels );
    }

#if 0
    /* Start stream if in prepared state */
    if( ioaudio_snd_pcm_state( stream->capture.pcm ) == SND_PCM_STATE_PREPARED )
    {
        ENSURE_( ioaudio_snd_pcm_start( stream->capture.pcm ), paUnanticipatedHostError );
    }
#endif

    while( frames > 0 )
    {
        int xrun = 0;
        PA_ENSURE( PaIoaudioStream_WaitForFrames( stream, &framesAvail, &xrun ) );
        framesGot = PA_MIN( framesAvail, frames );

        PA_ENSURE( PaIoaudioStream_SetUpBuffers( stream, &framesGot, &xrun ) );
        if( framesGot > 0 )
        {
            framesGot = PaUtil_CopyInput( &stream->bufferProcessor, &userBuffer, framesGot );
            PA_ENSURE( PaIoaudioStream_EndProcessing( stream, framesGot, &xrun ) );
            frames -= framesGot;
        }
    }

end:
    stream->playback.pcm = save;
    return result;
error:
    goto end;
}

static PaError WriteStream( PaStream* s, const void *buffer, unsigned long frames )
{
    PaError result = paNoError;
    signed long err;
    PaIoaudioStream *stream = (PaIoaudioStream*)s;
    snd_pcm_uframes_t framesGot, framesAvail;
    const void *userBuffer;
    snd_pcm_t *save = stream->capture.pcm;

    assert( stream );

    PA_UNLESS( stream->playback.pcm, paCanNotWriteToAnInputOnlyStream );

    /* Disregard capture */
    stream->capture.pcm = NULL;

    if( stream->underrun > 0. )
    {
        result = paOutputUnderflowed;
        stream->underrun = 0.0;
    }

    if( stream->playback.userInterleaved )
        userBuffer = buffer;
    else /* Copy channels into local array */
    {
        userBuffer = stream->playback.userBuffers;
        memcpy( (void *)userBuffer, buffer, sizeof (void *) * stream->playback.numUserChannels );
    }

    while( frames > 0 )
    {
        int xrun = 0;
        //snd_pcm_uframes_t hwAvail;

        PA_ENSURE( PaIoaudioStream_WaitForFrames( stream, &framesAvail, &xrun ) );
        framesGot = PA_MIN( framesAvail, frames );

        PA_ENSURE( PaIoaudioStream_SetUpBuffers( stream, &framesGot, &xrun ) );
        if( framesGot > 0 )
        {
            framesGot = PaUtil_CopyOutput( &stream->bufferProcessor, &userBuffer, framesGot );
            PA_ENSURE( PaIoaudioStream_EndProcessing( stream, framesGot, &xrun ) );
            frames -= framesGot;
        }

        /* Start stream after one period of samples worth */

        /* Frames residing in buffer */
        PA_ENSURE( err = GetStreamWriteAvailable( stream ) );
        framesAvail = err;
#if 0
        hwAvail = stream->playback.bufferSize - framesAvail;

        if( ioaudio_snd_pcm_state( stream->playback.pcm ) == SND_PCM_STATE_PREPARED &&
                hwAvail >= stream->playback.framesPerBuffer )
        {
            ENSURE_( ioaudio_snd_pcm_start( stream->playback.pcm ), paUnanticipatedHostError );
        }
#endif
    }

end:
    stream->capture.pcm = save;
    return result;
error:
    goto end;
}

/* Return frames available for reading. In the event of an overflow, the capture pcm will be restarted */
static signed long GetStreamReadAvailable( PaStream* s )
{
    PaError result = paNoError;
    PaIoaudioStream *stream = (PaIoaudioStream*)s;
    unsigned long avail;
    int xrun;

    PA_ENSURE( PaIoaudioStreamComponent_GetAvailableFrames( &stream->capture, &avail, &xrun ) );
    if( xrun )
    {
        PA_ENSURE( PaIoaudioStream_HandleXrun( stream ) );
        PA_ENSURE( PaIoaudioStreamComponent_GetAvailableFrames( &stream->capture, &avail, &xrun ) );
        if( xrun )
            PA_ENSURE( paInputOverflowed );
    }

    return (signed long)avail;

error:
    return result;
}

static signed long GetStreamWriteAvailable( PaStream* s )
{
    PaError result = paNoError;
    PaIoaudioStream *stream = (PaIoaudioStream*)s;
    unsigned long avail;
    int xrun;
    snd_pcm_channel_status_t status;

    PA_ENSURE( PaIoaudioStreamComponent_GetAvailableFrames( &stream->playback, &avail, &xrun ) );
    if( xrun )
    {
        snd_pcm_sframes_t savail;

        PA_ENSURE( PaIoaudioStream_HandleXrun( stream ) );
        status.channel = SND_PCM_CHANNEL_PLAYBACK;
        ioaudio_snd_pcm_channel_status( stream->playback.pcm, &status );
        savail = (snd_pcm_uframes_t)status.free;

        /* savail should not contain -EPIPE now, since PaIoaudioStream_HandleXrun will only prepare the pcm */
        ENSURE_( savail, paUnanticipatedHostError );

        avail = (unsigned long) savail;
    }

    return (signed long)avail;

error:
    return result;
}

/* Extensions */

void PaIoaudio_InitializeStreamInfo( PaIoaudioStreamInfo *info )
{
    info->size = sizeof (PaIoaudioStreamInfo);
    info->hostApiType = paIoaudio;
    info->version = 1;
    info->deviceString = NULL;
}

void PaIoaudio_EnableRealtimeScheduling( PaStream *s, int enable )
{
    PaIoaudioStream *stream = (PaIoaudioStream *) s;
    stream->rtSched = enable;
}

#if 0
void PaIoaudio_EnableWatchdog( PaStream *s, int enable )
{
    PaIoaudioStream *stream = (PaIoaudioStream *) s;
    stream->thread.useWatchdog = enable;
}
#endif

static PaError GetIoaudioStreamPointer( PaStream* s, PaIoaudioStream** stream )
{
    PaError result = paNoError;
    PaUtilHostApiRepresentation* hostApi;
    PaIoaudioHostApiRepresentation* ioaudioHostApi;

    PA_ENSURE( PaUtil_ValidateStreamPointer( s ) );
    PA_ENSURE( PaUtil_GetHostApiRepresentation( &hostApi, paIoaudio ) );
    ioaudioHostApi = (PaIoaudioHostApiRepresentation*)hostApi;

    PA_UNLESS( PA_STREAM_REP( s )->streamInterface == &ioaudioHostApi->callbackStreamInterface
            || PA_STREAM_REP( s )->streamInterface == &ioaudioHostApi->blockingStreamInterface,
        paIncompatibleStreamHostApi );

    *stream = (PaIoaudioStream*)s;
error:
    return result;
}

PaError PaIoaudio_GetStreamInputCard(PaStream* s, int* card) {
    PaIoaudioStream *stream;
    PaError result = paNoError;
    snd_pcm_info_t pcmInfo;

    PA_ENSURE( GetIoaudioStreamPointer( s, &stream ) );

    /* XXX: More descriptive error? */
    PA_UNLESS( stream->capture.pcm, paDeviceUnavailable );

    ioaudio_snd_pcm_info( stream->capture.pcm, &pcmInfo );
    *card = ioaudio_snd_card_name( pcmInfo.name );

error:
    return result;
}

PaError PaIoaudio_GetStreamOutputCard(PaStream* s, int* card) {
    PaIoaudioStream *stream;
    PaError result = paNoError;
    snd_pcm_info_t pcmInfo;

    PA_ENSURE( GetIoaudioStreamPointer( s, &stream ) );

    /* XXX: More descriptive error? */
    PA_UNLESS( stream->playback.pcm, paDeviceUnavailable );

    ioaudio_snd_pcm_info( stream->playback.pcm, &pcmInfo );
    *card = ioaudio_snd_card_name( pcmInfo.name );

error:
    return result;
}

PaError PaIoaudio_SetRetriesBusy( int retries )
{
    busyRetries_ = retries;
    return paNoError;
}
