# espressif/usb 1.6.0, patched for Deck32

Vendored from the component registry and used via `override_path` in
`components/usb_bridge/idf_component.yml`. Only `src/hcd_dwc.c` differs from upstream; every change is
marked `Deck32 patch`.

Background: on the ESP32-P4 high-speed port, high-speed interrupt OUT transfers (the Stream Deck's image
endpoint, 1024-byte packets every microframe) assert in `_buffer_parse_intr` or stall forever
(espressif/esp-idf#14412, open since 2024).

1. **HCCHAR.EC = 1 for periodic channels.** The HAL never sets the multi-count field. The DWC databook
   calls EC=0 reserved for periodic endpoints without split transactions ("undefined behavior").
2. **Retry NAKed interrupt OUT packets.** When the device NAKs (the deck does this while it decodes the
   previous image), the core retires the descriptor untouched: inactive, status success, the full length
   still remaining. It halts the channel without XferCompl, and the HAL reports that halt as a
   completion. Upstream then asserts. The NAKed packet also stays in the periodic TX FIFO, which shifts
   every later OUT out of alignment until the device stops accepting data. The patch handles it like
   Linux dwc2 does: flush the periodic TX FIFO and run the same single-packet descriptor again, giving up
   after about 5 s of consecutive NAKs.
3. **No assert on an unsent OUT.** Anything else that leaves an interrupt OUT descriptor unfinished is
   reported as `USB_TRANSFER_STATUS_ERROR`, and `usb_bridge` resends the report.

`deck32_intr_out_naks` counts patch-2 retries (logged at debug level by `usb_bridge`).

Ruled out along the way: driving the endpoint as bulk (the core starts PING after a NAK, and interrupt
endpoints don't answer PING) and sending output reports as SET_REPORT on EP0 (the MK.2 never answers).
