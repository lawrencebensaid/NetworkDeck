# Deck32: Network Dock emulator for ESP32-P4

Date: 2026-11-08
Status: approved design, awaiting spec review

## Goal

Deck32 is firmware that turns a Waveshare ESP32-P4-NANO (PoE) into a replacement for Elgato's
Stream Deck Network Dock (~€100). A Stream Deck plugs into the ESP's USB port. The official
Stream Deck app on macOS adds the ESP with "Add Network Device…" and drives the deck over the
network, as it would a real dock. Profiles and plugins keep running in the Stream Deck app unchanged.

**Success means you can't tell it from Elgato's dock.** The app pairs with it, your existing profiles
render and respond, and a key press reaches the app with no delay you can feel.

## Non-goals (v1)

- A standalone mode where the ESP runs profiles or plugins itself.
- A companion app (iOS or macOS).
- Boards other than the P4-NANO. ESP32-S3 boards come later as a new `board` file.
- Public release work: licensing, attribution, signed images, a GitHub update check. The project
  is private until the concept is proven.
- More than one host driving the deck at the same time.
- The older "legacy" TCP framing (the format the Stream Deck Studio falls back to). We speak Cora only.

## Constraints and facts

- **Hardware:**
  - ESP32-P4 rev v3.2 (ECO7 ROM), 400 MHz dual core, 16 MB flash, 32 MB PSRAM.
  - USB 2.0 high-speed OTG on a Type-A port, which is where the deck plugs in.
  - 100 Mbit Ethernet with PoE.
  - ESP32-C6 companion chip for Wi-Fi.
  - CH343 USB-serial chip on the Type-C port, for flashing and logs (`/dev/cu.usbmodem5CF71847081` on the dev Mac).
- **Toolchain:** ESP-IDF v6.1 at `~/esp/v6.1/esp-idf`, with the `esp32p4` target installed. Firmware is C++.
- **Test decks:**
  - The user's 15-key Stream Deck MK.2: VID `0x0FD9`, PID `0x0080`, runs at high speed (480 Mbit/s) on USB.
  - A white Stream Deck Mini.
- **Elgato's documented dock behaviour:**
  - Network settings live on the deck. The top-left keys show the IP address, firmware version and DHCP/static mode.
  - Static IP octets are edited by tapping them and using ↑ / ↓ keys.
  - The app pairs via "Add Network Device…" → enter the IP → "Pair Device". No code or confirmation is shown on the device.
  - Power is PoE 802.3af or USB-C PD. The deck gets 5 V at up to 0.7 A.
  - Supported decks: Mini, Neo, XL, +, + XL, MK.2, the second-generation original, and the Modules.
    Not supported: the first-generation original, Studio, and the XLR Docks.
- **No reference dock is available.** Protocol knowledge comes from three places:
  1. The client side in node-elgato-stream-deck `packages/tcp` (commit `7dd305c`, 2026-11-06).
  2. Logging what the official app sends to our emulator.
  3. Inspecting the Stream Deck app's binary and downloads when stuck.

## Protocol (what the Mac sees)

All multi-byte fields are little-endian. "Library" means node-elgato-stream-deck `packages/tcp`.

### Discovery

- mDNS service `_elg._tcp`, port 5343, TXT `dt=215` (Network Dock) and `sn=<serial>`.
- The instance name is the dock's display name, `Deck32-XXXX` (last 4 hex digits of the MAC).
- Adding the dock manually by IP must work without mDNS. That's the documented path in the official app.

### Cora framing

Every message has a 16-byte header followed by the payload:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `43 93 8A 41` |
| 4 | 2 | flags: `0x8000` VERBATIM (for the child deck), `0x4000` REQ_ACK, `0x0200` ACK_NAK, `0x0100` RESULT |
| 6 | 1 | hidOp: `0x00` write, `0x01` send feature report, `0x02` get feature report |
| 7 | 1 | reserved, `0` |
| 8 | 4 | messageId; replies echo the request's |
| 12 | 4 | payload length |

The dock sends first: the host picks the framing from the first bytes it receives. Every TCP connection
opens with a dock keepalive.

### Keepalive

- **Dock → host:** a keepalive payload starts `01 0A`, with the connection number at byte 5, and is longer than
  4 bytes. Sent every 1 s. That interval is our choice: the library only says the host gives up after 5 s of silence.
- **Host → dock:** the reply has flags ACK_NAK, the same hidOp and messageId, and a 32-byte payload `03 1A <connNo>`.
- **No acknowledgement from the host for 5 s means the host is gone.** The dock closes the socket and frees the
  host slot.

### Dock connection (TCP 5343)

The host sends get-feature-report requests with flags NONE and payload `03 <id>`. The dock replies with flags
RESULT, the echoed messageId, and a payload starting `03 <id>`. The host matches replies by `payload[1]`.

| id | Meaning | Fields the library reads | Our values |
|---|---|---|---|
| `0x80` | device info | VID u16 @12, PID u16 @14 | `0x0FD9`, `0xFFFF` |
| `0x83` | firmware version | ASCII @8..16 | stored setting, default `1.00.000`. In milestone 3 it is set to the latest real dock version, found in the app's update metadata, so the app sees nothing to update |
| `0x84` | serial | length @3, ASCII @4.. | 14 uppercase alphanumeric characters derived from the MAC |
| `0x85` | MAC | 6 bytes @4..10 | Ethernet MAC |
| `0x1C` | device 2 info | see below | |

**Device 2 info** (the reply payload starts `01 0B`):
- byte 4: `0x02` when a deck is attached and working, anything else when not
- VID u16 @26, PID u16 @28
- deck serial, ASCII, NUL-terminated, @94..125
- child TCP port u16 @126

**Plug events:** when a deck is attached or removed, the dock sends the same `01 0B` payload unprompted,
with no pending request.

Fields and lengths the library doesn't read are zero. The total payload length is 1024 bytes. That length is
an assumption to check against the official app in milestone 3.

Brightness and reset-to-logo requests aimed at the dock itself are accepted and do nothing.

### Deck connection (child port, TCP 5344)

Port 5344 is our choice; the dock tells the host which port to use in device 2 info.

The second connection reaches the plugged-in deck. Messages map onto USB like this:

| Host → dock | USB action | Dock → host |
|---|---|---|
| VERBATIM + write, payload = output report | interrupt OUT transfer (1024-byte report on gen2 decks) | — |
| VERBATIM + send feature report | `SET_REPORT(Feature, id=payload[0])` control transfer | — |
| VERBATIM + get feature report, payload `[id]` | `GET_REPORT(Feature, id)`, with the length taken from the HID report descriptor | VERBATIM + RESULT, echoed messageId, payload = report (starts with id) |
| — | interrupt IN report (key state etc.) | VERBATIM, payload = report (starts `01`) |

Rules on the child port:
- **Ignore** a non-verbatim `03 80` request; don't reply at all. The host fires `0x80`, `0x08` and `0xA1` in a
  race to find out whether this socket is the dock or the deck. A `0x80` answer would make it treat the deck
  connection as a second dock.
- **Answer** non-verbatim `03 1C` (device 2 info), the same as on the dock connection.
- The child connection's keepalive is the same as the dock connection's.

### Host policy

- There is one host slot, covering both connections.
- **First come, first served.** A connection from another IP address while the slot is taken is closed
  right after accept. A new connection from the host already in the slot replaces its old socket, because
  that host is reconnecting after a blip.
- The slot frees when keepalives fail or the socket closes, so a second Mac (for example the Mac mini) takes
  over within about 5 s of the first going to sleep.

### Elgato's firmware updates

- **Updates for the dock itself:** any request we don't recognise as part of the known protocol is logged in full
  and refused (no reply, or NAK where an acknowledgement was requested).
- **Updates for the deck:** traffic aimed at the deck passes through untouched, as on a real dock.

## Firmware architecture

ESP-IDF 6.1 project in C++. Each unit is its own component with one job and a small interface.

```
firmware/
  main/                app_main: wiring, task startup
  components/
    cora/              protocol core. No ESP-IDF code; builds on the Mac for unit tests
    usb_bridge/        USB host for Elgato decks
    dock_server/       TCP listeners, host slot, routing
    net/               Ethernet, Wi-Fi via C6, setup hotspot, mDNS
    deck_ui/           dock-owned screens and the per-model image table
    web/               status page, Wi-Fi setup, firmware upload (embedded gzipped HTML)
    ota/               firmware update slots, rollback
    settings/          stored settings (NVS): network mode, static IP, Wi-Fi, serial, firmware version string
    board/             P4-NANO pins and peripherals (from Waveshare's board support package)
  partitions.csv       nvs, otadata, phy_init, ota_0 (4 MB), ota_1 (4 MB), spare
  sdkconfig.defaults
tools/dock-client/     Node test client using @elgato-stream-deck/tcp
```

### Units

- **`cora`:** frame parser and serializer (streaming, resynchronizes on the magic bytes), keepalive state machine,
  replies for the dock connection, and the device 2 info builder.
  - Input: bytes from the socket, plus the current deck state.
  - Output: frames to send, and deck-bound actions (`Write`, `SetFeature`, `GetFeature`).
  - Pure C++17, so it builds on the Mac for unit tests.
- **`usb_bridge`:** a small driver of our own on the ESP-IDF USB host library, rather than the HID class
  driver, because we need raw access to the OUT endpoint and feature reports.
  - Claims an Elgato (VID `0x0FD9`) HID interface and parses its report descriptor for feature report sizes.
  - Runs the interrupt IN reader and the interrupt OUT writer.
  - The USB hardware FIFO is configured for 1024-byte periodic OUT transfers.
  - Interface: `on_attach(info)`, `on_detach()`, `on_input(report)`, `write(report)`,
    `set_feature(report)`, `get_feature(id) → report`.
  - Who owns the deck: the dock server while a host is connected, otherwise `deck_ui`.
- **`dock_server`:** runs both listeners, the host slot and the keepalive timers.
  - **Flow control:** it stops reading a socket while the USB OUT queue is full. Nothing is ever dropped;
    TCP slows the host down.
  - Hands the deck to `deck_ui` when no host is connected.
  - **Trace mode:** every frame in and out goes to the serial log (header plus the first 32 bytes) and to a
    ring buffer the web interface shows. This is how we reverse-engineer the official app.
- **`net`:**
  - Ethernet is preferred. With no Ethernet link after 5 s at boot (or when the link is lost), the dock
    joins the saved Wi-Fi through the C6 (esp-hosted). With no saved Wi-Fi, it starts the setup hotspot.
  - The setup hotspot is `Deck32-XXXX`, WPA2, with a random 8-character password made at each boot. It serves
    the setup page at `http://192.168.4.1/`.
  - DHCP or a static address (IP, netmask, gateway; DNS = gateway).
  - mDNS is announced on whichever link is active and re-announced when the link changes.
- **`deck_ui`:** the dock's own screens, drawn only while it owns the deck.
  - **Per-model table:** keys, grid, key size in pixels, image format (JPEG or BMP), rotation, and image write
    header. Values are taken from node-elgato-stream-deck `core/src/models/definitions.ts`. JPEG uses the P4's
    hardware encoder.
  - Text rendering uses one embedded bitmap font. Layouts are made for 15 keys (5×3) and scale down to the
    Mini's 6 keys (3×2).
- **`web`:** esp_http_server.
  - `GET /` status: link, IP, host connected or not, deck model, firmware version.
  - Wi-Fi setup: scan, choose a network, enter the password.
  - Firmware upload: `.bin`.
  - `/trace`: the protocol ring buffer.
  - Viewing is open to anyone. Every change goes through a confirmation on the deck (below).
- **`ota`:** two app slots, the bootloader's rollback, and the new image marked valid once it has booted and
  started its network. No image signing in v1.
- **`settings`:** stored settings (NVS), with defaults and a factory reset.

### Tasks

USB host daemon + USB client, TCP server (one task per connection), net events, web server, and deck UI. They
communicate through FreeRTOS queues, and the task watchdog covers all of them.

## On-deck behaviour

### No host connected

The Elgato-style network screen in the top-left keys:
- IP address
- link type (Ethernet or Wi-Fi)
- firmware version
- a mode key: "DHCP" (blue) or "Static"

The other keys are blank.

**Static editing:** tapping the mode key switches DHCP ↔ Static. In Static mode, tapping an octet selects it,
↑ / ↓ change it, and ✓ moves to the next field: IP → netmask → gateway → save and apply.

Elgato's exact layout is known only from 540-px screenshots, so it's matched as closely as they allow.

### Setup hotspot active

The deck shows:
- a QR code across the keys that joins the hotspot (`WIFI:T:WPA;S:Deck32-XXXX;P:<pw>;;`)
- the hotspot name and password as text
- `192.168.4.1`

### Confirming a change from the web interface

- The deck shows the change ("Join Wi-Fi 'Home'?" / "Install firmware 1.2?") with ✓ and ✕ keys.
  It is cancelled automatically after 30 s.
- If a host is connected, the dock takes the deck for the prompt and stops forwarding key presses to the app.
  Afterwards it sends a virtual unplug and plug, so the app redraws its profile. That keeps the bridge
  transparent without caching key images. To be checked in milestone 3.
- **Exception:** changes made through the setup hotspot need no confirmation. Joining the hotspot already
  proves the person is nearby, because its password is only shown on the deck.

### Factory reset

Holding the P4-NANO's BOOT button for 10 s erases the network and Wi-Fi settings and reboots.

## Errors and recovery

| Situation | Behaviour |
|---|---|
| Deck unplugged | Device 2 info says not attached; plug event sent; the deck socket closes; the dock connection stays up |
| Deck plugged in | Enumerate, then plug event. If no host is connected, `deck_ui` draws |
| USB transfer error / stall | Clear the stall, then retry once, then reset the port and re-enumerate |
| Host silent for 5 s | Close both sockets, free the slot, `deck_ui` takes the deck |
| Ethernet lost | Switch to Wi-Fi if one is saved; re-announce on mDNS; the host reconnects to the new IP (DHCP reservation recommended) |
| Unknown request from the host | Log the full frame; no reply |
| Image fails to boot | The bootloader rolls back to the previous slot |
| Task hangs | The watchdog reboots the ESP |

## Testing

- **Unit (Mac):** `cora` builds with the Mac's clang and doctest (`firmware/components/cora/test`). It covers
  framing (split and merged TCP segments, resync on the magic bytes), keepalive timing, every reply layout
  above, and device 2 info for attached and detached decks.
- **Integration (known-good client):** `tools/dock-client` drives the real ESP through `@elgato-stream-deck/tcp`.
  It runs discovery, reads the dock info, connects to the deck, fills all keys, reads key presses,
  sets brightness, and simulates unplug and replug. It also measures key-press latency and full-deck
  redraw frame rate, on both Ethernet and Wi-Fi.
- **Official app:** pair through "Add Network Device…", load the user's existing profiles, and watch `/trace` for unknown requests until there are none.
- **Hardware:** MK.2 and Mini; PoE power; deck power at 0.7 A on the Type-A port.

**Targets, measured and not assumed:**
- Key press → app, under 30 ms on wired LAN.
- Full 15-key redraw at 10 fps or better over Ethernet.
- The app reconnects without user action after a dock reboot.

## Milestones

1. **USB bridge.** The MK.2 enumerates, a test pattern is drawn and key presses are printed on the serial log.
   We check the USB power budget.
2. **Dock protocol.** `cora` passes its unit tests. `dock-client` can discover the dock, read its info, draw and
   read keys over Ethernet.
3. **Official app.** It pairs, the user's profiles render and work, and the unknown requests are resolved.
   We confirm the virtual-replug redraw and the device 2 info payload length.
4. **Deck screens.** Network screen, static IP editor and confirmation prompts, on both the MK.2 and the Mini.
5. **Wi-Fi, hotspot, web interface, firmware upload.** Ethernet → Wi-Fi fallback, the setup hotspot with its
   QR code, the web interface, firmware updates with rollback, and factory reset.

## Risks

- **Hidden checks in the official app:** pairing steps, serial or firmware format checks, or authentication
  that the library doesn't use. Mitigation: trace mode in milestone 3, inspection of the app's binary, and a
  configurable serial and firmware version string. If the app needs real cryptographic authentication,
  the project stops at milestone 3, and `dock-client` / Bitfocus Companion remain usable hosts.
- **USB host on the P4's high-speed port:** periodic OUT transfers of 1024 bytes need the FIFO setting, and hubs
  are not usable on that port. Mitigation: milestone 1 tests this first, with the deck plugged straight in.
- **C6 Wi-Fi firmware version:** the C6's esp-hosted firmware must match the version on the P4. Mitigation:
  check at boot, and update the C6 from the P4 if needed (milestone 5).
- **Wi-Fi throughput:** full-deck redraws may be slower than on Ethernet. Measured in milestone 5, and
  accepted if it's within the targets.
- **Elgato's exact network screen layout:** we only have low-resolution screenshots, so the dock looks close
  rather than pixel-identical.
