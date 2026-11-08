# Changelog

What users can see or do, newest release first. A pull request that changes something users notice
adds a line under "Unreleased"; the release that raises `VERSION` renames that section
`## [x.y.z] - YYYY-MM-DD`.

## Unreleased

### Added
- Password-protected web access, with physical approval for first-use setup, expiring browser
  sessions and sign-out. Status, traces, key images and firmware uploads require sign-in.
- Firmware uploads reject unauthenticated requests before flash writes, serialize OTA writers,
  and stop stalled uploads. The web server still uses HTTP: use a trusted, isolated network.

## [0.1.0] - 2026-11-08

First release: the Waveshare ESP32-P4-NANO works as a Stream Deck Network Dock.

### Added
- The official Stream Deck app pairs with the dock ("Add Network Device…") and drives a Stream Deck
  MK.2 plugged into the dock's USB-A port: your profiles and plugins run unchanged.
- Bitfocus Companion and other `@elgato-stream-deck/tcp` clients work too.
- Ethernet (PoE or USB power) with DHCP or a static address; Wi-Fi when no cable is connected; a
  setup hotspot with a QR code on the deck when there is no network to join.
- The dock's own screens on the deck while no app is connected: IP address, network, firmware, and
  DHCP/static mode with an on-deck static IP editor.
- Web interface: status, Wi-Fi setup, network settings and firmware upload with automatic rollback.
  Changes are confirmed on the deck.
- Factory reset: hold the top-left and top-right keys for 10 seconds, then confirm.
