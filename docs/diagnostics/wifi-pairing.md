# Wi-Fi pairing investigation

Repository-only findings; these are not a diagnosis of the installed device. Neither the installed
build nor the exact macOS rejection message was available during this investigation. The Ethernet
cable was disconnected during the user's Wi-Fi test.

## Confirmed defects addressed here

- `main.cpp::publish_net` previously replaced every DHCP netmask with 255.255.255.0. The net task
  now reads the selected Ethernet, station or hotspot netif's actual `esp_netif_get_ip_info` tuple
  and publishes its address and mask together. Mask-only renewals trigger an update as well.
- A `set_network` callback could precede `dock_server::start`, after which the initial Config
  assignment overwrote it. Server initialization now takes the same mutex and preserves any
  network tuple already published, including a deliberately empty tuple after link loss.

This matters to report 0x87: it contains IP at bytes 4 and 12 and mask at byte 8. A non-/24 lease
previously produced incorrect protocol data. Whether the macOS app rejects that mismatch needs
hardware evidence; this is not a claim that it explains all Wi-Fi failures.

## Other candidates, not changed in this PR

1. The single-host slot compares the remote peer IPv4 address. A Mac changing interfaces/source IP
   can be rejected while its previous connection is alive. Normal idle sockets expire after five
   seconds, but blocking USB work can delay the connection loop's timeout processing. Also, a failed
   connection-task allocation clears pointers without clearing the owner IP: that is a separate
   confirmed cleanup bug and can strand the slot if no other connection remains to clean it up.
   The existing logs `no memory for connection task` followed by `refused dock connection from ...`
   distinguish this case. If the Mac's source IP does not change, the listener instead replaces
   the same-host connection; simply unplugging the dock's Ethernet does not imply a new Mac IP.
2. The network task publishes interface changes asynchronously after IP events. There can be a
   short interval between Wi-Fi getting a lease and the report snapshot reflecting it. This PR
   fixes the deterministic startup overwrite, not every event-to-query timing race.
3. Wi-Fi power-save is not explicitly configured. ESP-IDF normally defaults to MIN_MODEM; the
   actual ESP-Hosted/C5 state is unknown. Sleep latency alone does not establish a five-second
   timeout. Do not disable it or stretch timeouts without measurement.

## mDNS and interface identity

The app registers `_elg._tcp` with only `dt=215` and the stable dock serial in TXT. It does not inject
an Ethernet IP into mDNS. The declared espressif/mdns ^1.15.1 dependency's v1.15.1 implementation
lazily resolves `WIFI_STA_DEF`, handles `IP_EVENT_STA_GOT_IP`, and enables the station IPv4 PCB.
Default predefined STA/AP/Ethernet support is enabled. Missing explicit re-announcement code in
NetworkDeck is therefore not by itself evidence of broken Wi-Fi discovery. The actual resolved
component version, generated configuration and multicast/AP behavior still need verification.

Listeners bind INADDR_ANY and are started before mDNS registration. Wi-Fi is started on a separate
network task after the initial five-second Ethernet wait, so it need not be ready when listeners
start. Lazy mDNS discovery supports this ordering. The dock serial/MAC intentionally remain stable
across interfaces; no repository protocol evidence requires swapping them to the Wi-Fi radio MAC.
The protocol implementation follows open-source implementations and observed app traffic, not a
published official guarantee for this emulator's Wi-Fi compatibility.

## Decisive evidence

- Exact macOS rejection wording and installed firmware build.
- One connection sequence containing either `refused dock connection from ...` (especially after
  `no memory for connection task`) or `host silent for 5000 ms` / `closed by host` / socket error.
- The new `link Wi-Fi, IP ..., advertised mask ...` log paired with the actual DHCP lease can
  verify this patch after an independently built, explicitly authorized hardware test.

## Validation

Three additional native protocol test cases cover non-/24 wire masks, interface address changes
with stable serial/MAC, and mask-only renewal. They do not exercise FreeRTOS or the netif driver.
Native tests and the ESP-IDF firmware build were not runnable in this environment: no C++ compiler,
CMake or ESP-IDF/container runtime; repository CI has no registered runner. This must remain a draft
until those builds and hardware pairing are verified. No changes to power saving, timeouts, USB
streaming, host admission policy, CI, deployment or hardware are included.

## Public references

- [mDNS v1.15.1 interface lookup and IP-event handling](https://github.com/espressif/esp-protocols/blob/mdns-v1.15.1/components/mdns/mdns_netif.c)
- [mDNS v1.15.1 predefined-interface defaults](https://github.com/espressif/esp-protocols/blob/mdns-v1.15.1/components/mdns/Kconfig)
- [ESP-IDF v6.1 Wi-Fi power-save API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/network/esp_wifi.html)
