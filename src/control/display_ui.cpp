#include "control/display_ui.hpp"
#include "defines.hpp"
#include <stdint.h>
#include <stdio.h>

namespace dummer
{
namespace control
{

namespace
{

// RGB565 colour constants
constexpr uint16_t kBlack  = 0x0000u;
constexpr uint16_t kWhite  = 0xFFFFu;
constexpr uint16_t kGreen  = 0x07E0u;
constexpr uint16_t kRed    = 0xF800u;
constexpr uint16_t kYellow = 0xFFE0u;
constexpr uint16_t kDGrey  = 0x4208u;

// Layout — landscape 320 W × 170 H (all values in pixels)
//
//  y=  4  Style name (font 4, 26 px)  +  BPM  +  speed dots
//  y= 36  REV: state        VOX: count (font 2, 16 px)
//  y= 60  LOOP: state label           (font 2, 16 px)
//  y= 80  Progress bar      (h=12)
//  y= 96  Loop time text    (font 2, 16 px)
//  y=120  CPU avg / max     (font 1,  8 px)

constexpr int16_t kY_Style   =   4;
constexpr int16_t kY_RevVox  =  36;
constexpr int16_t kY_LoopLbl =  60;
constexpr int16_t kY_Bar     =  80;
constexpr int16_t kBarH      =  12;
constexpr int16_t kY_LoopT   =  96;
constexpr int16_t kY_Cpu     = 120;

constexpr int16_t kBarX = 4;
constexpr int16_t kBarW = 312;

// AutoStyle numeric order (matches auto_drummer.hpp):
// 0=Off 1=Blues 2=Country 3=Jazz 4=Funk 5=Reggae 6=Gospel 7=HardRock
static const char* const kStyleNames[8] = {
    "OFF", "Blues", "Country", "Jazz", "Funk", "Reggae", "Gospel", "HardRock"
};

// LooperState numeric order (matches looper.hpp):
// 0=Idle 1=Arming 2=Recording 3=Playing
static const char* const kLooperStr[4] = {"IDLE", "ARMING", "REC", "PLAYING"};

static void fmt_time(char* buf, size_t n, uint32_t samples)
{
    const uint32_t secs = samples / static_cast<uint32_t>(SAMPLE_RATE);
    snprintf(buf, n, "%u:%02u", static_cast<unsigned>(secs / 60u),
             static_cast<unsigned>(secs % 60u));
}

static void fmt_ms(char* buf, size_t n, uint32_t us)
{
    // Cap display at 9999.9 ms to bound the output length (≤8 chars + null).
    const unsigned ms_whole = static_cast<unsigned>((us / 1000u) < 9999u ? us / 1000u : 9999u);
    const unsigned ms_frac  = static_cast<unsigned>((us % 1000u) / 100u);
    snprintf(buf, n, "%u.%ums", ms_whole, ms_frac);
}

} // namespace

DisplayUi::DisplayUi(hal::IDisplay& disp) : disp_(disp) {}

void DisplayUi::init()
{
    disp_.init();
    disp_.fill_rect(0, 0, disp_.width(), disp_.height(), kBlack);
}

void DisplayUi::render(const DisplayState& s)
{
    const bool force = first_render_;
    first_render_    = false;

    const bool style_ch =
        force || s.style != prev_.style || s.auto_active != prev_.auto_active ||
        s.speed != prev_.speed || s.bpm != prev_.bpm;

    const bool rv_vox_ch =
        force || s.reverb_on != prev_.reverb_on || s.voice_count != prev_.voice_count;

    const bool loop_ch =
        force || s.looper_state != prev_.looper_state ||
        s.loop_length_samples != prev_.loop_length_samples ||
        s.pos_samples != prev_.pos_samples;

    const bool cpu_ch =
        force || s.cpu_max_us != prev_.cpu_max_us || s.cpu_avg_us != prev_.cpu_avg_us;

    if (style_ch)   draw_style(s);
    if (rv_vox_ch)  draw_reverb_voices(s);
    if (loop_ch)    draw_looper(s);
    if (cpu_ch)     draw_profiler(s);

    prev_ = s;
}

void DisplayUi::draw_style(const DisplayState& s)
{
    disp_.fill_rect(0, kY_Style - 2, disp_.width(), 30, kBlack);

    const uint8_t  si = (s.style < 8) ? s.style : 0u;
    const uint16_t fg = s.auto_active ? kGreen : kDGrey;

    disp_.draw_text(4, kY_Style, kStyleNames[si], 4, fg, kBlack);

    if (s.auto_active && s.bpm > 0)
    {
        char buf[10];
        snprintf(buf, sizeof(buf), "%3u BPM", static_cast<unsigned>(s.bpm));
        disp_.draw_text(168, kY_Style, buf, 4, kWhite, kBlack);
    }

    // Three speed dots (●=filled ○=dark); positioned at right of row A
    for (uint8_t i = 0; i < 3u; ++i)
    {
        const uint16_t dc = (s.auto_active && i <= s.speed) ? kWhite : kDGrey;
        disp_.fill_rect(static_cast<int16_t>(284 + i * 10), kY_Style + 18, 6, 6, dc);
    }
}

void DisplayUi::draw_reverb_voices(const DisplayState& s)
{
    disp_.fill_rect(0, kY_RevVox - 2, disp_.width(), 20, kBlack);

    const uint16_t rc = s.reverb_on ? kGreen : kDGrey;
    disp_.draw_text(4, kY_RevVox, s.reverb_on ? "REV: ON" : "REV:OFF", 2, rc, kBlack);

    char vbuf[14];
    snprintf(vbuf, sizeof(vbuf), "VOX:%2u/50", static_cast<unsigned>(s.voice_count));
    disp_.draw_text(192, kY_RevVox, vbuf, 2, kWhite, kBlack);
}

void DisplayUi::draw_looper(const DisplayState& s)
{
    disp_.fill_rect(0, kY_LoopLbl - 2, disp_.width(), 56, kBlack);

    const uint8_t ls = (s.looper_state < 4u) ? s.looper_state : 0u;

    uint16_t lc = kDGrey;
    if (ls == 1) lc = kYellow;
    else if (ls == 2) lc = kRed;
    else if (ls == 3) lc = kGreen;

    char lbuf[20];
    snprintf(lbuf, sizeof(lbuf), "LOOP: %s", kLooperStr[ls]);
    disp_.draw_text(4, kY_LoopLbl, lbuf, 2, lc, kBlack);

    if (ls == 2 || ls == 3) // Recording or Playing
    {
        const uint32_t total = (ls == 3)
            ? s.loop_length_samples
            : static_cast<uint32_t>(LOOPER_MAX_DURATION_S) * static_cast<uint32_t>(SAMPLE_RATE);
        const uint32_t pos = s.pos_samples;

        int16_t filled = (total > 0u)
            ? static_cast<int16_t>(
                  static_cast<uint32_t>(kBarW) *
                  (static_cast<uint64_t>(pos < total ? pos : total) * 1000u / total) /
                  1000u)
            : 0;
        if (filled > kBarW) filled = kBarW;

        const uint16_t bar_col = (ls == 2) ? kRed : kGreen;
        disp_.fill_rect(kBarX, kY_Bar, filled, kBarH, bar_col);
        disp_.fill_rect(kBarX + filled, kY_Bar, static_cast<int16_t>(kBarW - filled), kBarH, kDGrey);

        char tbuf[24];
        if (ls == 3)
        {
            char t1[8], t2[8];
            fmt_time(t1, sizeof(t1), pos);
            fmt_time(t2, sizeof(t2), s.loop_length_samples);
            snprintf(tbuf, sizeof(tbuf), "%s / %s", t1, t2);
        }
        else
        {
            fmt_time(tbuf, sizeof(tbuf), pos);
        }
        disp_.draw_text(4, kY_LoopT, tbuf, 2, kWhite, kBlack);
    }
}

void DisplayUi::draw_profiler(const DisplayState& s)
{
    disp_.fill_rect(0, kY_Cpu - 2, disp_.width(), 14, kBlack);

    char abuf[10], mbuf[10], pbuf[40];
    fmt_ms(abuf, sizeof(abuf), s.cpu_avg_us);
    fmt_ms(mbuf, sizeof(mbuf), s.cpu_max_us);
    snprintf(pbuf, sizeof(pbuf), "CPU avg:%s  max:%s", abuf, mbuf);
    disp_.draw_text(4, kY_Cpu, pbuf, 1, kDGrey, kBlack);
}

} // namespace control
} // namespace dummer
