# NetworkDeck REST API

The web interface runs on this API, and scripts and other tools can use it too. It is served at
`http://<name>.local` and `https://<name>.local` (the dock's own certificate; trust `/ca.pem` once).

## Authentication

Make an API key in **Settings → API keys**. The dock shows it once and keeps only its SHA-256 hash.
Send it with every request:

```
Authorization: Bearer ndk_0123456789abcdef0123456789abcdef01234567
```

A key can also be made without signing in, by asking the dock and pressing ✓ on the deck:

```sh
curl -X POST -H 'X-NetworkDeck: 1' -d 'name=My script' http://deck32-204c.local/api/keys
```

The answer has the key in `key`. At most 8 keys; making and using a key (its first use since the dock
started) is logged.

The web interface itself signs in with the password or a passkey and gets a session cookie instead.
Requests that change something with a cookie must also send `X-NetworkDeck: 1`. With an API key that
header is not needed.

## Conventions

- Bodies of `POST` requests are `application/x-www-form-urlencoded` (except `/api/update`).
- Answers are JSON. Changes answer `{"ok": true|false, "message": "…"}`; `message` is meant for people.
- `401` without a valid key or session, `403` without the header a cookie request needs, `409` when
  the dock refuses (the message says why), `429` while sign-in is locked after wrong passwords.
- Every change to the dock's settings is confirmed on the deck (✓ or ✕, 30 seconds), so those requests
  can take that long to answer. Requests from the dock's setup network need no confirmation, and
  neither do changes on chips without a USB host (the ESP32-C6), where there is never a deck to ask.

## Endpoints

### Status and the deck

| Method | Path | What |
|---|---|---|
| GET | `/api/status` | The dock: name, serial, firmware, link, IP, Wi-Fi, hostname, addressing, and the Stream Deck app's computer (`host`, `host_ip`, `host_name`, `host_seconds`; the name from the router's reverse DNS or the computer's NetBIOS name). |
| GET | `/api/deck` | The deck on the USB port: `attached`, `pid`, `model`, `serial`, `cols`, `rows`, `key_px`, `finish`, who draws (`source`: `app` or `dock`), each key's version or colour (`keys`), which keys are pressed (`pressed`), and `quiet_ms` since the app last changed a key. |
| GET | `/api/key?n=<key>&v=<version>` | The image on a key (JPEG or BMP, in the deck's orientation; `transform` in `/api/deck` turns it upright). `204` for a key with no image. |
| POST | `/api/deck/brightness` | `value` 0–100: the brightness of the dock's own screens. |
| POST | `/api/deck/finish` | `value` `black` or `white`: how the web interface draws this deck (remembered by serial). |

### Log

| Method | Path | What |
|---|---|---|
| GET | `/api/log?before=<seq>&limit=<n>` | Records, newest first: `s` sequence, `b` start number, `t` Unix time (0 before the clock was set), `u` seconds since that start, `c` category (`system`, `update`, `network`, `connection`, `deck`, `security`, `matter`), `l` level (`info`, `warn`, `error`), `m` message. `limit` at most 500. |
| GET | `/api/trace` | The last protocol frames between the Stream Deck app and the dock, as text. |

### Network

| Method | Path | What |
|---|---|---|
| POST | `/api/network` | `mode` `dhcp` or `static`; for static also `ip`, `mask`, `gateway`. Confirmed on the deck. |
| POST | `/api/hostname` | `value`: the `<name>.local` hostname; empty for the default. |
| GET | `/api/scan` | Wi-Fi networks in range: `[{ssid, rssi, secure}]`. Takes a few seconds. |
| POST | `/api/wifi` | `ssid`, `password`: join this network. Confirmed on the deck. |
| POST | `/api/wifi/enabled` | `value` `1` or `0`. Turning Wi-Fi off is confirmed on the deck. |
| POST | `/api/wifi/forget` | Forget the saved network. Confirmed on the deck. |

### System

| Method | Path | What |
|---|---|---|
| GET | `/api/system` | The hardware: `chip`, `revision`, `cores`, `cpu_mhz`, `flash_bytes`, `psram_bytes`, `board`, what it can do (`usb_host`, `ethernet`, `matter`), `serial`, MAC addresses, `firmware`, `uptime`, `reset_reason`, memory (`heap_free`, `heap_min`, `heap_total`, `psram_free`), `temp_c` (and the `warm_c` / `hot_c` thresholds), and the options: `led`, `led_gpio`, `usb_power_gpio`. |
| POST | `/api/system` | Any of `led` (`1`/`0`, at once), `led_gpio`, `usb_power_gpio` (a GPIO number or `none`; at the next start). |
| POST | `/api/restart` | Restart the dock. Confirmed on the deck. |
| GET / POST | `/api/rest` | The REST API connector: `{"enabled"}`; `value` `1`/`0` turns it on or off (while off, API keys are refused). |

### Matter

| Method | Path | What |
|---|---|---|
| GET | `/api/matter` | `enabled`, `running`, `fabrics` (controllers), `window_open`, `qr`, `manual`. |
| POST | `/api/matter/setup` | Turn Matter on. |
| POST | `/api/matter/open` | Open pairing for 15 minutes. |
| POST | `/api/matter/off` | Leave every controller, forget the setup code and restart. Confirmed on the deck. |

### Firmware

| Method | Path | What |
|---|---|---|
| POST | `/api/update` | The body is `networkdeck-<chip>-v<version>.bin` (`application/octet-stream`). Checked, confirmed on the deck, then the dock restarts into it; if it doesn't start, the dock goes back. |

### Users and keys

| Method | Path | What |
|---|---|---|
| GET | `/api/keys` | `[{id, name, created, last_used}]`. |
| POST | `/api/keys` | `name`: make a key (confirmed on the deck). The answer's `key` is shown only this once. |
| POST | `/api/keys/delete` | `id`: delete a key. |
| GET | `/api/passkeys` | The passkeys that can sign in. |
| POST | `/api/password` | `current`, `password`: change the password; other browsers are signed out. |

Sign-in (`/api/auth`, `/api/login`, `/api/logout`, `/api/setup`, `/api/passkey/*`) is for the web
interface and not meant for scripts.
