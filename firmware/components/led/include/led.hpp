// The board's addressable RGB LED (WS2812), as a status light: blue while the dock is in setup or has
// no network, green when it is ready, purple while the Stream Deck app is connected.
#pragma once

namespace led {

enum class State { Off, Setup, Ready, AppConnected };

// `gpio` < 0: no LED. Call once.
void start(int gpio);
void set(State s);

}  // namespace led
