/* delay_test.cpp -- Munchi Delay driven as the chain drives an audio FX,
 * against a fake host clock at 120 BPM (transport stopped: free-running).
 *
 *   delay_test <out.wav>
 */
#include "../src/dsp/munchi_delay.cpp"
#include <unistd.h>
#include <vector>

static float fake_bpm(void) { return 120.f; }

static int fails = 0;
static void check(const char *what, bool ok)
{
    printf("%-44s %s\n", what, ok ? "ok" : "FAILED");
    fails += !ok;
}

int main(int argc, char **argv)
{
    static host_api_v1_t host;
    memset(&host, 0, sizeof(host));
    host.sample_rate = 44100;
    host.get_bpm     = fake_bpm;
    audio_fx_api_v2_t *api = move_audio_fx_init_v2(&host);
    void *fx = api->create_instance(".", nullptr);
    for(int i = 0; i < 300 && !((MunchiDelay *)fx)->ready.load(); i++)
        usleep(10000);
    check("buffers allocated", ((MunchiDelay *)fx)->ready.load());

    std::vector<int16_t> rec;
    int16_t blk[256];
    // 1. noon: bit-transparent
    bool same = true;
    for(int b = 0; b < 200; b++)
    {
        int16_t ref[256];
        for(int i = 0; i < 256; i++)
            blk[i] = ref[i] = (int16_t)((rand() % 20000) - 10000);
        api->process_block(fx, blk, 128);
        same = same && !memcmp(ref, blk, sizeof(blk));
    }
    check("knob at noon: bit-transparent", same);

    // 2. delay side, on a fresh instance (TEMPO writes into its buffer even
    // with the effect off, so the noise above would come back as echoes):
    // a click, then listen for the repeats
    api->destroy_instance(fx);
    fx = api->create_instance(".", nullptr);
    for(int i = 0; i < 300 && !((MunchiDelay *)fx)->ready.load(); i++)
        usleep(10000);
    api->set_param(fx, "wand", "-0.900"); // far left: shortest interval
    api->set_param(fx, "feedback", "0.5");
    for(int b = 0; b < 40; b++) // let the wet amount settle
    {
        memset(blk, 0, sizeof(blk));
        api->process_block(fx, blk, 128);
    }
    int click_at = (int)rec.size() / 2;
    for(int b = 0; b < 700; b++) // ~2 s
    {
        memset(blk, 0, sizeof(blk));
        if(b == 0)
            for(int i = 0; i < 8; i++)
                blk[i * 2] = blk[i * 2 + 1] = 20000;
        api->process_block(fx, blk, 128);
        rec.insert(rec.end(), blk, blk + 256);
    }
    // first echo after the click itself, on the right channel
    int first = -1;
    for(size_t i = (size_t)(click_at + 2000); i < rec.size() / 2; i++)
        if(abs(rec[i * 2 + 1]) > 1500)
        {
            first = (int)i - click_at;
            break;
        }
    // TEMPO: delay_samples = 60e6/(2*bpm) us * .192 * delayDivs[pos]; far
    // left is pos 1 (1/6) -> 250000 * .192 / 6 = 8000 samples at 48 kHz,
    // and the right channel reads a further 480 back (delayStereoOffsetRight)
    double ms = first * 1000.0 / 44100.0;
    printf("first echo after %.1f ms\n", ms);
    check("an echo arrives", first > 0);
    check("at the clocked interval (~177 ms at 120)", fabs(ms - 176.7) < 15);

    // 3. freeze holds the buffer: feed silence, it keeps sounding
    api->set_param(fx, "freeze", "1");
    for(int b = 0; b < 100; b++)
    {
        memset(blk, 0, sizeof(blk));
        api->process_block(fx, blk, 128);
    }
    char buf[256];
    api->get_param(fx, "freeze", buf, sizeof(buf));
    check("freeze engaged", atoi(buf) == 1);

    // 3b. Thaw = Gap: unfreezing mutes the repeats for an interval. The
    // firmware's mute fade wrote through a null pointer when it ended; on
    // the CHOMPI a harmless store to flash, here a crash.
    api->set_param(fx, "unfreeze_mute", "Gap");
    api->set_param(fx, "freeze", "0");
    double gap_energy = 0;
    for(int b = 0; b < 100; b++)
    {
        memset(blk, 0, sizeof(blk));
        api->process_block(fx, blk, 128);
        for(int i = 0; i < 256; i++)
            gap_energy += (double)blk[i] * blk[i];
    }
    check("thaw gap: silent after unfreeze, no crash", gap_energy == 0);
    api->set_param(fx, "unfreeze_mute", "Now");

    // 4. right side: reverb tail on a click
    api->set_param(fx, "freeze", "0");
    api->set_param(fx, "wand", "0.9");
    for(int b = 0; b < 400; b++)
    {
        memset(blk, 0, sizeof(blk));
        if(b == 50)
            for(int i = 0; i < 8; i++)
                blk[i * 2] = blk[i * 2 + 1] = 20000;
        api->process_block(fx, blk, 128);
        rec.insert(rec.end(), blk, blk + 256);
    }
    // 5. state
    api->set_param(fx, "random", "0.4");
    api->set_param(fx, "clock", "2x");
    int n = api->get_param(fx, "state", buf, sizeof(buf));
    std::string st(buf, n);
    printf("state: %s\n", st.c_str());
    void *fx2 = api->create_instance(".", nullptr);
    api->set_param(fx2, "state", st.c_str());
    n = api->get_param(fx2, "state", buf, sizeof(buf));
    check("state round-trips", std::string(buf, n) == st);
    for(int i = 0; i < 300 && !((MunchiDelay *)fx2)->ready.load(); i++)
        usleep(10000);
    api->destroy_instance(fx2);
    api->destroy_instance(fx);

    if(argc > 1)
    {
        FILE *of = fopen(argv[1], "wb");
        uint32_t dl = rec.size() * 2, v;
        uint8_t  h[44] = {'R', 'I', 'F', 'F'};
        v = 36 + dl; memcpy(h + 4, &v, 4);
        memcpy(h + 8, "WAVEfmt ", 8);
        v = 16; memcpy(h + 16, &v, 4);
        uint16_t s16 = 1; memcpy(h + 20, &s16, 2);
        s16 = 2; memcpy(h + 22, &s16, 2);
        v = 44100; memcpy(h + 24, &v, 4);
        v = 44100 * 4; memcpy(h + 28, &v, 4);
        s16 = 4; memcpy(h + 32, &s16, 2);
        s16 = 16; memcpy(h + 34, &s16, 2);
        memcpy(h + 36, "data", 4);
        memcpy(h + 40, &dl, 4);
        fwrite(h, 1, 44, of);
        fwrite(rec.data(), 2, rec.size(), of);
        fclose(of);
    }
    printf("%s\n", fails ? "FAILED" : "delay ok");
    return fails;
}
