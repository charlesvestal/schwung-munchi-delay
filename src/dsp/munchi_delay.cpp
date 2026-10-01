/* munchi_delay.cpp -- CHOMPI TEMPO's Dual Delay as a Schwung audio effect.
 *
 * TEMPO's Magic Wand knob drives one effect: a clock-synced delay that,
 * turned the other way, blends into a diffusion reverb, with random octave /
 * reverse / pan events, a buffer freeze, and a sidechain that ducks the
 * repeats under new material once the feedback is high. This is that effect,
 * on the firmware's own code (tempo/granularDelay.h, reverb.h, fx_engine.h,
 * SimpleCompressor.h), wired the way FxEngine::Process wired it for one
 * engine (tempo/upstream/FxEngine.h).
 *
 *   clock      TEMPO's clock came from its own timer; here it is Move's tempo
 *              (host->get_bpm) and, while the transport runs, its beat
 *              position (host->get_beat_position), as 24 pulses per beat
 *   rate       the effect runs at its native 48 kHz. Only the wet path is
 *              resampled: the dry signal passes at 44.1 kHz untouched, so with
 *              the knob at noon the effect is bit-transparent
 *
 * Threading: create_instance allocates the instance and starts a loader that
 * demotes itself (SCHED_OTHER, cores 0-2), allocates and touches the 7.7 MB
 * of delay buffers, initialises the delay and reverb, and publishes `ready`.
 * Until then audio passes dry.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <sched.h>
#include <atomic>
#include <new>
#include <string>
#include <thread>

#include "plugin_api_v1.h"
#include "audio_fx_api_v2.h"
#include "tempo/granularDelay.h"
#include "tempo/reverb.h"
#include "tempo/SimpleCompressor.h"
#include "resampler.h"

static const host_api_v1_t *g_host = nullptr;

static inline int16_t f2s16(float x)
{
    // the inverse of the 1/32768 read, so an untouched sample comes back as
    // the same integer
    float v = x * 32768.f;
    v       = v < -32768.f ? -32768.f : v > 32767.f ? 32767.f : v;
    return (int16_t)lrintf(v);
}

static constexpr size_t kBufferSize = 480000; // chompi_main.cpp: 10 s at 48 kHz, stereo below

enum
{
    K_WAND, K_RANDOM, K_FEEDBACK, K_MIX, K_FREEZE, K_CLOCK, K_UNFREEZE_MUTE,
    K_COUNT
};

struct ParamDef
{
    const char *key;
    float       min, max, def;
    int         options; // 0: float; else an enum with this many options
};

static const ParamDef kDefs[K_COUNT] = {
    {"wand", -1.f, 1.f, 0.f, 0},
    {"random", 0.f, 1.f, 0.f, 0},
    {"feedback", 0.f, 1.f, .3f, 0},
    {"mix", 0.f, 1.f, .5f, 0},
    {"freeze", 0.f, 1.f, 0.f, 2},
    {"clock", 0.f, 2.f, 1.f, 3},
    {"unfreeze_mute", 0.f, 1.f, 0.f, 2},
};
static const char *kOnOff[2]  = {"Off", "On"};
static const char *kClock[3]  = {"1/2x", "1x", "2x"};
static const char *kThaw[2]   = {"Now", "Gap"}; // TEMPO's "Delay Buffer Unfreeze Mute"
static const float kClockMul[3] = {.5f, 1.f, 2.f};

struct MunchiDelay
{
    granularDelay    delay;
    daisysp::Reverb  reverb;
    SimpleCompressor comp;
    clockManager     clock;
    float           *granular = nullptr, *frozen = nullptr;

    std::thread       loader;
    std::atomic<bool> ready{false};
    bool              applied = false;

    float p[K_COUNT];

    // FxEngine's per-engine state, as TEMPO's CHROMA engine had it
    float wet_amt = 1.f, wet_target = 1.f, dry48 = 1.f, dry_target = 1.f, dry44 = 1.f;
    float reverb_amt = 0.f, reverb_amt_target = 0.f;
    float reverb_boost = 0.f, reverb_boost_target = 0.f;

    // clock pulses
    double pulse_phase  = 0.0;
    long   pulses       = 0;
    bool   had_transport = false;

    munchi::StreamResampler up, down;
    float wet_l[1024], wet_r[1024];
    int   wet_count = 0;
};

/* ---- FxEngine's knob mappings --------------------------------------------- */

static void set_main(MunchiDelay *d, float val) // FxEngine::setGranularMain
{
    d->delay.setMainControl(val);
    if(val > .55f)
    {
        float norm             = (val - .55f) / (1.f - .55f);
        d->reverb_amt_target   = .25f + .65f * powf(norm, .5f);
        d->reverb_boost_target = val > .6f ? .3f : 0.f;
    }
    else
    {
        d->reverb_amt_target   = 0.f;
        d->reverb_boost_target = 0.f;
    }
}

static void set_mix(MunchiDelay *d, float val) // FxEngine::setGranularMix
{
    if(val >= .5f)
    {
        d->dry_target = 2.f - val * 2.f;
        d->wet_target = 1.f;
    }
    else
    {
        d->dry_target = 1.f;
        d->wet_target = val * 2.f;
    }
}

static void set_feedback(MunchiDelay *d, float val) // FxEngine::setGranularFeedback
{
    d->delay.setFeedback(val);
    float norm = val > .6f ? (val - .6f) / (1.f - .6f) : 0.f;
    d->comp.setAmount(norm);
}

static void apply(MunchiDelay *d, int k)
{
    const float v = d->p[k];
    switch(k)
    {
        case K_WAND: set_main(d, .5f + v * .5f); break;
        case K_RANDOM: d->delay.setAltControl(v); break;
        case K_FEEDBACK: set_feedback(d, v); break;
        case K_MIX: set_mix(d, v); break;
        case K_FREEZE:
            // TEMPO's short press toggles, through a crossfade, and only
            // while the delay runs
            if((v > .5f) != d->delay.getBufferLock())
                d->delay.toggleBufferLock();
            break;
        case K_CLOCK: break; // read every block
        case K_UNFREEZE_MUTE: d->delay.setMuteOption(v > .5f); break;
    }
}

/* ---- loader ---------------------------------------------------------------- */

static void loader_main(MunchiDelay *d)
{
#ifdef __linux__
    struct sched_param sp = {};
    sched_setscheduler(0, SCHED_OTHER, &sp); // first: we inherited FIFO 70
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    CPU_SET(1, &set);
    CPU_SET(2, &set);
    sched_setaffinity(0, sizeof(set), &set);
#endif
    d->granular = (float *)malloc(kBufferSize * 2 * sizeof(float));
    d->frozen   = (float *)malloc(kBufferSize * 2 * sizeof(float));
    if(!d->granular || !d->frozen)
        return; // stays dry
    // touch every page here, not on the audio thread
    memset(d->granular, 0, kBufferSize * 2 * sizeof(float));
    memset(d->frozen, 0, kBufferSize * 2 * sizeof(float));
    d->reverb.Init(48000.f);
    d->reverb.SetAmount(0.f);
    d->reverb.SetInputGain(.3f);
    d->reverb.SetLowpass(1.f);
    d->comp.Init();
    d->delay.Init(d->granular, d->frozen, kBufferSize, &d->clock, false);
    d->up.Init(44100.0, 48000.0);
    d->down.Init(48000.0, 44100.0);
    // the wet path's head start: the resamplers deliver in bursts
    d->wet_count = 96;
    d->ready.store(true, std::memory_order_release);
}

/* ---- the clock ------------------------------------------------------------- */

static void clock_pulse(MunchiDelay *d)
{
    // TEMPO's timer callback: every tick a pulse; every 12th (an eighth note,
    // engine 0 at x1) a clock edge
    d->delay.setClockPulse();
    d->pulses++;
    if(d->pulses % 12 == 0)
        d->delay.setClockEdge();
}

static void run_clock(MunchiDelay *d, int frames)
{
    const int   ci  = (int)lrintf(d->p[K_CLOCK]);
    const float mul = kClockMul[ci < 0 ? 0 : ci > 2 ? 2 : ci];
    float       bpm = g_host && g_host->get_bpm ? g_host->get_bpm() : 120.f;
    if(!(bpm > 0.f))
        bpm = 120.f;
    // the delay buffer holds 10 s: below 40 BPM the 2-bar interval would not fit
    if(bpm * mul < 40.f)
        bpm = 40.f / mul;
    d->clock.setBpm(bpm * mul);

    double beat = g_host && g_host->get_beat_position ? g_host->get_beat_position() : -1.0;
    if(beat >= 0.0)
    {
        // transport running: follow Move's beat, so edges land on its grid
        long target = (long)floor(beat * 24.0 * mul);
        if(!d->had_transport || target < d->pulses || target - d->pulses > 96)
            d->pulses = target; // start, restart or a jump: re-anchor
        int n = 0;
        while(d->pulses < target && n++ < 48)
            clock_pulse(d);
        d->had_transport = true;
        return;
    }
    d->had_transport = false;
    // stopped: run free at the tempo, as TEMPO's own clock did
    d->pulse_phase += bpm * mul * 24.0 / 60.0 * frames / 44100.0;
    while(d->pulse_phase >= 1.0)
    {
        d->pulse_phase -= 1.0;
        clock_pulse(d);
    }
}

/* ---- audio ------------------------------------------------------------------ */

static inline void process48(MunchiDelay *d, float xl, float xr, float *ol, float *or_)
{
    fonepole(d->wet_amt, d->wet_target, .001f);
    fonepole(d->dry48, d->dry_target, .001f);
    fonepole(d->reverb_amt, d->reverb_amt_target, .001f);
    fonepole(d->reverb_boost, d->reverb_boost_target, .001f);

    float wl = xl * d->wet_amt, wr = xr * d->wet_amt;
    float dl = xl * d->dry48, dr = xr * d->dry48; // the ducking sidechain
    float yl = 0.f, yr = 0.f;
    d->delay.write(wl, wr);
    d->delay.read(&yl, &yr);
    d->reverb.SetAmount(d->reverb_amt * d->reverb_amt * .8f);
    d->reverb.SetTime(d->reverb_amt);
    d->reverb.SetLowpass(d->reverb_amt * .55f + .4f);
    d->reverb.SetDiffusion(d->reverb_amt * .6f);
    yl += wl * d->reverb_boost;
    yr += wr * d->reverb_boost;
    d->reverb.Process(&yl, &yr);
    d->comp.Process(&yl, &yr, &dl, &dr);
    *ol = yl;
    *or_ = yr;
}

static void fx_process_block(void *instance, int16_t *io, int frames)
{
    MunchiDelay *d = (MunchiDelay *)instance;
    if(!d || !d->ready.load(std::memory_order_acquire))
        return; // dry until the buffers are in
    if(!d->applied)
    {
        d->applied = true;
        for(int k = 0; k < K_COUNT; k++)
            apply(d, k);
    }
    run_clock(d, frames);

    for(int i = 0; i < frames; i++)
    {
        d->up.Push(io[i * 2] * (1.f / 32768.f), io[i * 2 + 1] * (1.f / 32768.f));
        while(d->up.CanPull())
        {
            float xl, xr, yl, yr;
            d->up.Pull(&xl, &xr);
            process48(d, xl, xr, &yl, &yr);
            d->down.Push(yl, yr);
        }
        while(d->down.CanPull() && d->wet_count < 1024)
        {
            d->down.Pull(&d->wet_l[d->wet_count], &d->wet_r[d->wet_count]);
            d->wet_count++;
        }
    }
    const int n = frames < d->wet_count ? frames : d->wet_count;
    for(int i = 0; i < frames; i++)
    {
        fonepole(d->dry44, d->dry_target, .001f);
        float wl = i < n ? d->wet_l[i] : 0.f, wr = i < n ? d->wet_r[i] : 0.f;
        float l  = io[i * 2] * (1.f / 32768.f) * d->dry44 + wl;
        float r  = io[i * 2 + 1] * (1.f / 32768.f) * d->dry44 + wr;
        io[i * 2]     = (int16_t)f2s16(l);
        io[i * 2 + 1] = (int16_t)f2s16(r);
    }
    memmove(d->wet_l, d->wet_l + n, (d->wet_count - n) * sizeof(float));
    memmove(d->wet_r, d->wet_r + n, (d->wet_count - n) * sizeof(float));
    d->wet_count -= n;
}

/* ---- params ---------------------------------------------------------------- */

static int find_key(const char *key)
{
    for(int k = 0; k < K_COUNT; k++)
        if(!strcmp(kDefs[k].key, key))
            return k;
    return -1;
}

static void set_value(MunchiDelay *d, int k, float v)
{
    const ParamDef &def = kDefs[k];
    if(def.options)
        v = roundf(v);
    d->p[k] = daisysp::fclamp(v, def.min, def.max);
    if(d->applied)
        apply(d, k);
}

static bool json_number(const char *json, const char *key, float *out)
{
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if(!p)
        return false;
    p += strlen(pat);
    while(*p == ' ' || *p == ':' || *p == '"')
        p++;
    char *end;
    float v = strtof(p, &end);
    if(end == p)
        return false;
    *out = v;
    return true;
}

static void fx_set_param(void *instance, const char *key, const char *val)
{
    MunchiDelay *d = (MunchiDelay *)instance;
    if(!d || !key || !val)
        return;
    if(!strcmp(key, "state"))
    {
        float v;
        for(int k = 0; k < K_COUNT; k++)
            if(json_number(val, kDefs[k].key, &v))
                set_value(d, k, v);
        return;
    }
    int k = find_key(key);
    if(k < 0)
        return;
    float v;
    if(kDefs[k].options && !(val[0] >= '0' && val[0] <= '9'))
    {
        const char **names = k == K_CLOCK ? kClock : k == K_UNFREEZE_MUTE ? kThaw : kOnOff;
        v                  = kDefs[k].def;
        for(int i = 0; i < kDefs[k].options; i++)
            if(!strcasecmp(val, names[i]))
                v = (float)i;
    }
    else
        v = strtof(val, nullptr);
    set_value(d, k, v);
}

static int fx_get_param(void *instance, const char *key, char *buf, int buf_len)
{
    MunchiDelay *d = (MunchiDelay *)instance;
    if(!d || !key)
        return -1;
    if(!strcmp(key, "name"))
        return snprintf(buf, buf_len, "Munchi Delay");
    int k = find_key(key);
    if(k >= 0)
    {
        if(k == K_FREEZE && d->applied) // what the delay is doing, not what was asked
            return snprintf(buf, buf_len, "%d", d->delay.getBufferLock() ? 1 : 0);
        if(kDefs[k].options)
            return snprintf(buf, buf_len, "%d", (int)lrintf(d->p[k]));
        return snprintf(buf, buf_len, "%.3f", d->p[k]);
    }
    if(!strcmp(key, "state"))
    {
        std::string out = "{\"v\":1";
        char        tmp[64];
        for(int i = 0; i < K_COUNT; i++)
        {
            // a freeze is a moment, not a setting: never restore one
            if(i == K_FREEZE)
                continue;
            snprintf(tmp, sizeof(tmp), ",\"%s\":%.4f", kDefs[i].key, d->p[i]);
            out += tmp;
        }
        out += "}";
        if((int)out.size() >= buf_len)
            return -1;
        memcpy(buf, out.c_str(), out.size() + 1);
        return (int)out.size();
    }
    return -1;
}

/* ---- lifecycle --------------------------------------------------------------- */

static void *fx_create_instance(const char *module_dir, const char *config_json)
{
    (void)module_dir;
    (void)config_json;
    // TEMPO's classes have empty constructors and leave their members to the
    // CHOMPI's zeroed RAM (delay_on_, lock_buffer_, mute_, the write heads):
    // zero the memory, then construct in it
    void *mem = calloc(1, sizeof(MunchiDelay));
    if(!mem)
        return nullptr;
    MunchiDelay *d = new(mem) MunchiDelay();
    for(int k = 0; k < K_COUNT; k++)
        d->p[k] = kDefs[k].def;
    d->loader = std::thread(loader_main, d);
    return d;
}

static void fx_destroy_instance(void *instance)
{
    MunchiDelay *d = (MunchiDelay *)instance;
    if(!d)
        return;
    if(d->loader.joinable())
        d->loader.join();
    free(d->granular);
    free(d->frozen);
    d->~MunchiDelay();
    free(d);
}

static audio_fx_api_v2_t g_api;

extern "C" __attribute__((visibility("default"))) audio_fx_api_v2_t *
move_audio_fx_init_v2(const host_api_v1_t *host)
{
    g_host = host;
    memset(&g_api, 0, sizeof(g_api));
    g_api.api_version      = AUDIO_FX_API_VERSION_2;
    g_api.create_instance  = fx_create_instance;
    g_api.destroy_instance = fx_destroy_instance;
    g_api.process_block    = fx_process_block;
    g_api.set_param        = fx_set_param;
    g_api.get_param        = fx_get_param;
    return &g_api;
}
