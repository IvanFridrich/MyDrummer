#pragma once
#include "hal/hal.hpp"
#include "control/display_state.hpp"

namespace dummer
{
namespace control
{

// Stateful UI renderer for the 320×170 landscape display.
// Tracks previous state; only redraws regions that changed (partial refresh).
// Call init() once after the display is powered, then render() at ~10 Hz.
// No heap allocation — all string formatting uses local stack buffers.
class DisplayUi
{
  public:
    explicit DisplayUi(hal::IDisplay& disp);
    void init();
    void render(const DisplayState& s);

  private:
    hal::IDisplay& disp_;
    DisplayState   prev_{};
    bool           first_render_ = true;

    void draw_style(const DisplayState& s);
    void draw_reverb_voices(const DisplayState& s);
    void draw_looper(const DisplayState& s);
    void draw_profiler(const DisplayState& s);
};

} // namespace control
} // namespace dummer
