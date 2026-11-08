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

Protocol notes and design: `docs/superpowers/specs/`. The USB host driver carries one fix for the
P4's high-speed interrupt endpoints: `firmware/vendor/usb/DECK32_PATCH.md`.
