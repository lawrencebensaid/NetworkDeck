# Changelog

What users can see or do, newest release first. A pull request that changes something users notice
adds a line under "Unreleased"; the release that raises `VERSION` renames that section
`## [x.y.z] - YYYY-MM-DD`.

## Unreleased

### Added
- A new web interface: the Stream Deck on the dock is shown live in the middle, with what the
  Stream Deck app (or the dock) has on its keys and which key is pressed. Lights in the top bar show
  the deck and the network; the settings and the integrations open as windows.
- The Stream Deck's screen brightness (while it shows the dock's screens) can be set in the web interface.
- The dock's own screens on the deck look like the Elgato Network Dock's: IP, Firmware, a lit Mode key
  and Setup.
- Wi-Fi can be turned off, and the Wi-Fi settings show the saved network, its signal and a list to
  choose from (hidden networks too); the saved network can be forgotten.
- The hostname (`<name>.local`) is shown and can be changed.
- Matter: add the dock to Home Assistant, Apple Home or Google Home with the QR code in the web
  interface (Integrations). It is off until you choose Set up Matter. Each key is a button there (press, release, long press) for automations.

### Changed
- Key images reach the deck about six times faster: full-deck redraws went from 3–4 to over 20 a second.

### Security
- HTTPS at `https://<name>.local`, with a certificate from the dock's own certificate authority. Trust
  it once (download it in Settings → Users); it can only vouch for this dock's names and private
  addresses.
- Passkeys: sign in with Touch ID, Face ID, a phone or a security key (Settings → Users, at the
  HTTPS address). Adding one asks for the password again.
- The web interface and firmware upload need a password. You choose it the first time you open the
  interface and confirm it on the deck; five wrong tries lock sign-in for 30 seconds.

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
