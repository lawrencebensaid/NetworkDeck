# Changelog

What users can see or do, newest release first. A pull request that changes something users notice
adds a line under "Unreleased"; the release that raises `VERSION` renames that section
`## [x.y.z] - YYYY-MM-DD`.

## Unreleased

### Changed
- Key images reach the deck about six times faster: full-deck redraws went from 3–4 to over 20 a second.

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
