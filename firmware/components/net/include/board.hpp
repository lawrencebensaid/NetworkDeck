// Waveshare ESP32-P4-NANO pins. Ethernet: IP101GRI PHY over RMII, 50 MHz reference clock from the PHY.
// Verify against Waveshare's schematic if the link doesn't come up.
#pragma once

namespace board {
inline constexpr int kEthMdc = 31;
inline constexpr int kEthMdio = 52;
inline constexpr int kEthRmiiClk = 50;
inline constexpr int kEthPhyReset = 51;
inline constexpr int kBootButton = 35;
}  // namespace board
