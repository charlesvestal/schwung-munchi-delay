/* temp_led_stuff.h -- the colour crossfades from TEMPO's LED helper, which
 * granularDelay::getColors() uses. Verbatim; the LED driver is not here. */
#pragma once
inline float color_xfade(float start, float end, float idx)
{
    return (1.f - idx) * start + idx * end;
}

inline float color_triple_xfade(float start, float mid, float end, float idx)
{
    if(idx < .5f)
    {
        idx *= 2.f;
        return color_xfade(start, mid, idx);
    }
    else
    {
        idx = (idx - .5f) * 2.f;
        return color_xfade(mid, end, idx);
    }
}

inline float color_quad_xfade(float start, float mid1, float mid2, float end, float idx)
{
    if(idx < .33f)
    {
        idx *= 3.f;
        return color_xfade(start, mid1, idx);
    }
    else if(idx < .66f)
    {
        idx = (idx - .33f) * 3.f;
        return color_xfade(mid1, mid2, idx);
    }
    else
    {
        idx = (idx - .66f) * 3.f;
        return color_xfade(mid2, end, idx);
    }
}
