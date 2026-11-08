# espressif/usb 1.6.0, patched for Deck32

Vendored from the component registry and used via `override_path` in
`components/usb_bridge/idf_component.yml`. The only file that differs from upstream is `src/hcd_dwc.c`,
and it has one change, marked `Deck32 patch`.

## RX FIFO size (`_calculate_fifo_from_bias`, PERIODIC_OUT bias)

Upstream gives the RX FIFO exactly one 512-byte packet plus 2 status lines (130 lines). A high-speed
512-byte interrupt IN endpoint needs at least 3 spare lines (espressif/esp-idf#19143, reported on an
Ajazz AKP153E, a Stream Deck clone with the same endpoints). With only 2 spare lines:

- the IN channel never receives data: key presses are lost, and "completed" transfers with 0 bytes come
  back thousands of times a second;
- interrupt OUT (key images) asserts in `_buffer_parse_intr` or stalls (espressif/esp-idf#14412).

The patch gives RX two packets plus status (264 of 896 lines on the P4). The rest goes to the periodic TX
FIFO (568 lines, room for two 1024-byte OUT packets), and the non-periodic TX FIFO keeps 64 lines.

Remove this vendored copy once upstream fixes #19143.
