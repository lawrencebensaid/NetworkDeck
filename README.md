# NetworkDeck

A Stream Deck Network Dock on a Waveshare ESP32-P4-NANO (PoE). Plug a Stream Deck into the board's
USB-A port; the official Stream Deck app adds it with "Add Network Device…" and drives it over the
network, so your profiles and plugins run unchanged — no laptop next to the deck.

- **Works with:** the Stream Deck app (macOS, tested with 7.6), Bitfocus Companion and anything built
  on `@elgato-stream-deck/tcp`. Decks: Stream Deck MK.2 (tested); the Mini's screens are in place.
- **Network:** Ethernet with PoE, Wi-Fi when there's no cable, and a setup hotspot (QR code on the
  deck) when there's nothing to join.
- **On the deck** while no app is connected: IP address, network, firmware, DHCP/static, an on-deck
  static IP editor, and confirmations for changes made in the web interface.
- **Web interface** at the dock's address: status, Wi-Fi, network settings, firmware upload with
  automatic rollback.

## Install

Download `networkdeck-<version>-full.bin` from the [latest release](../../releases/latest) and write it at
offset 0 over the board's Type-C port:

```sh
python -m esptool --chip esp32p4 -p /dev/cu.usbmodem<…> -b 460800 write-flash 0x0 networkdeck-<version>-full.bin
```

Later updates: upload `networkdeck-<version>.bin` in the web interface.

## Build

ESP-IDF v6.1. From `firmware/`: `idf.py build flash monitor`. Host tests (protocol core, HID parser,
deck screens) run on the Mac or Linux:

```sh
cmake -S firmware/test/host -B build/host -G Ninja && cmake --build build/host && build/host/deck32_tests
```

`tools/dock-client` is a test host built on `@elgato-stream-deck/tcp` (discover, read the dock,
draw, read keys, benchmark).

## Development

Pull requests go into `develop`; merging `develop` into `main` releases when `VERSION` was raised
(see `.github/workflows/firmware.yml` and `CHANGELOG.md`). CI runs on the self-hosted runner in the k3s
cluster (`deploy/ci-runner/`).

The USB host driver carries one fix for the
P4's high-speed interrupt endpoints: `firmware/vendor/usb/DECK32_PATCH.md`.

## Web interface access

On first use (including an upgrade from v0.1), the browser shows only password setup.
Choose a unique password of 12–128 UTF-8 bytes and approve **Set web interface password?**
on the physical deck within 30 seconds. The setup hotspot does not bypass this approval.
Until setup/sign-in succeeds, status, Wi-Fi scans, protocol traces, key images, network changes
and firmware uploads are inaccessible. The TCP Stream Deck protocol is unchanged.

The password is stored as a versioned, salted PBKDF2-HMAC-SHA256 verifier (100,000 iterations),
not plaintext. A browser session expires after 30 minutes; a new login invalidates the previous
session. Sign out clears the session; restarting the dock invalidates all sessions. Login attempts
are serialized and limited to at most one per second. Firmware uploads require authentication
before flash writes, only one update may run at a time, and uploads have a bounded deadline.
Existing physical installation confirmation and OTA validation/rollback remain in place.

**This is access control, not encrypted transport.** The server still uses HTTP on port 80:
passwords and session cookies can be intercepted on the network. Use only a trusted, isolated
LAN/setup hotspot, never reuse an important password, and do not forward this port to the Internet.
Neither this change nor the password protects the separate Stream Deck TCP service from untrusted
LAN clients. TLS and signed firmware/secure boot are separate work.

If the password is lost, use the existing physical factory reset on the deck. It erases the web
password together with network and Wi-Fi settings; there is no unauthenticated reset endpoint.
Unreadable or malformed stored credentials fail closed instead of silently enabling setup.

Browser behavior tests (Node.js 18+):

```sh
node --test firmware/test/web/auth-ui.test.cjs
```

The host test target also includes password bounds, cookie parsing, expiry and token comparison
regressions. Before flashing authentication changes, build with ESP-IDF v6.1 and verify physical
setup approval/rejection, password persistence after reboot, sign-in/out, expiry, rejected uploads
(no flash writes), a valid authenticated OTA, and factory reset on actual hardware.
