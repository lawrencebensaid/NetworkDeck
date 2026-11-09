/*
 * The Stream Deck hardware the stage draws, by USB product id (ids, key grids and controls from
 * Elgato's HID docs, docs.elgato.com/streamdeck/hid). Sizes are millimetres on the front of the deck.
 *
 * Measured from product photos, to within about half a millimetre: the 15-key decks (black and white
 * MK.2), the Mini, and the + XL. The XL, +, Neo, Studio and Pedal have no photo measurement yet: their
 * bodies use the published outer size and the key size and pitch of the measured decks (16 mm keys on
 * a 19.5 mm pitch), so they are close but not exact.
 */

export type Finish = "black" | "white"

/* Other things on the front, drawn under the keys: a touch strip or screen, a recessed tray, dials. */
export interface Part {
  kind: "screen" | "tray" | "dial" | "touch" | "pedal"
  x: number
  y: number
  w: number
  h: number
  radius?: number
}

export interface Hardware {
  name: string
  cols: number
  rows: number
  w: number
  h: number
  radius: number
  key: number /* the key cap */
  gap: number
  top: number /* top edge to the first row */
  /* the Elgato Stream Deck logo: its centre's distance from the top edge, and its width (0: none) */
  logoY: number
  logoW: number
  parts: Part[]
  finish: Finish
  measured: boolean
}

const PITCH = 19.5

function grid(cols: number, w: number, key: number, pitch = PITCH) {
  const gap = pitch - key
  return { gap, left: (w - (cols * key + (cols - 1) * gap)) / 2 }
}

const fifteen: Omit<Hardware, "name"> = {
  cols: 5, rows: 3, w: 118, h: 84, radius: 3.4, key: 16, gap: grid(5, 118, 16).gap, top: 17,
  logoY: 8.2, logoW: 41, parts: [], finish: "black", measured: true,
}
const six: Omit<Hardware, "name"> = {
  cols: 3, rows: 2, w: 84, h: 60, radius: 3.4, key: 16.3, gap: 3.2, top: 12.3,
  logoY: 5.8, logoW: 32.7, parts: [], finish: "black", measured: true,
}
const thirtyTwo: Omit<Hardware, "name"> = {
  cols: 8, rows: 4, w: 182, h: 112, radius: 4.5, key: 16, gap: grid(8, 182, 16).gap, top: 22,
  logoY: 11, logoW: 41, parts: [], finish: "black", measured: false,
}

/* The + XL: 9x4 keys, the touch strip in a black panel, six dials in a recessed tray. */
const plusXlDials: Part[] = Array.from({ length: 6 }, (_, i) => ({ kind: "dial", x: 28.8 + i * 29.6 - 7.5, y: 150 - 7.5, w: 15, h: 15 }))
const plusXl: Omit<Hardware, "name"> = {
  cols: 9, rows: 4, w: 205, h: 184, radius: 6.5, key: 16.1, gap: 3.4, top: 13.7,
  logoY: 6.7, logoW: 41.4,
  parts: [
    { kind: "screen", x: 15.1, y: 96.3, w: 177.5, h: 24.2, radius: 0.6 },
    { kind: "tray", x: 10, y: 133.4, w: 185, h: 35.5, radius: 9 },
    ...plusXlDials,
  ],
  finish: "black", measured: true,
}

/* The +: 4x2 larger keys, the touch strip, four dials. */
const plus: Omit<Hardware, "name"> = {
  cols: 4, rows: 2, w: 140, h: 140, radius: 6, key: 20.5, gap: 6, top: 15,
  logoY: 7.5, logoW: 34,
  parts: [
    { kind: "screen", x: 16.25, y: 69, w: 107.5, h: 16.5, radius: 0.6 },
    { kind: "tray", x: 10, y: 96, w: 120, h: 34, radius: 9 },
    ...Array.from({ length: 4 }, (_, i): Part => ({ kind: "dial", x: 16.25 + 13.25 + i * 26.5 - 8, y: 113 - 8, w: 16, h: 16 })),
  ],
  finish: "black", measured: false,
}

/* The Neo: 4x2 keys, an info window between two touch points. */
const neo: Omit<Hardware, "name"> = {
  cols: 4, rows: 2, w: 109, h: 78, radius: 4, key: 17, gap: 4.5, top: 13,
  logoY: 6.5, logoW: 30,
  parts: [
    { kind: "screen", x: 33.5, y: 56.5, w: 42, h: 10, radius: 0.6 },
    { kind: "touch", x: 13.5, y: 57.5, w: 14, h: 8 },
    { kind: "touch", x: 81.5, y: 57.5, w: 14, h: 8 },
  ],
  finish: "black", measured: false,
}

const TABLE: Record<number, Hardware> = {
  0x0060: { name: "Stream Deck", ...fifteen },
  0x006d: { name: "Stream Deck", ...fifteen },
  0x0080: { name: "Stream Deck MK.2", ...fifteen },
  0x00a5: { name: "Stream Deck MK.2 Scissor Keys", ...fifteen },
  0x00b9: { name: "Stream Deck 15-Key Module", ...fifteen },
  0x0063: { name: "Stream Deck Mini", ...six },
  0x0090: { name: "Stream Deck Mini", ...six },
  0x00b3: { name: "Stream Deck Mini Discord Edition", ...six },
  0x00b8: { name: "Stream Deck 6-Key Module", ...six },
  0x006c: { name: "Stream Deck XL", ...thirtyTwo },
  0x008f: { name: "Stream Deck XL", ...thirtyTwo },
  0x00ba: { name: "Stream Deck 32-Key Module", ...thirtyTwo },
  0x0084: { name: "Stream Deck +", ...plus },
  0x00c6: { name: "Stream Deck + XL", ...plusXl },
  0x009a: { name: "Stream Deck Neo", ...neo },
  0x0086: {
    name: "Stream Deck Pedal", cols: 0, rows: 0, w: 230, h: 110, radius: 18, key: 0, gap: 0, top: 0, logoY: 0, logoW: 0,
    parts: [
      { kind: "pedal", x: 14, y: 26, w: 58, h: 70, radius: 8 },
      { kind: "pedal", x: 80, y: 14, w: 70, h: 82, radius: 8 },
      { kind: "pedal", x: 158, y: 26, w: 58, h: 70, radius: 8 },
    ],
    finish: "black", measured: false,
  },
  0x00aa: {
    name: "Stream Deck Studio", cols: 16, rows: 2, w: 440, h: 44, radius: 3, key: 13, gap: 3.6, top: 7.4, logoY: 0, logoW: 0,
    parts: [
      { kind: "dial", x: 31, y: 7, w: 30, h: 30 },
      { kind: "dial", x: 379, y: 7, w: 30, h: 30 },
    ],
    finish: "black", measured: false,
  },
}

/* A deck the table doesn't know: an MK.2-like body around the keys the dock reports. */
function generic(cols: number, rows: number, name: string): Hardware {
  const key = 16
  const gap = PITCH - key
  const w = cols * key + (cols - 1) * gap + 24
  return { ...fifteen, name, cols, rows, w, h: rows * key + (rows - 1) * gap + 17 + 12, measured: false }
}

export function hardware(pid: number | undefined, cols?: number, rows?: number, name?: string): Hardware {
  const known = pid !== undefined ? TABLE[pid] : undefined
  if (known) return known
  return generic(cols ?? 5, rows ?? 3, name ?? "Stream Deck")
}

/*
 * The finish of the deck that is plugged in. A black and a white deck of one model have the same USB ids,
 * and neither USB nor Elgato's HID reports carry a colour, so the dock remembers what was chosen for each
 * deck (by serial, in the Stream Deck panel). Until then: a product string that says "white", or the
 * model's own finish.
 */
export function finishOf(h: Hardware, chosen?: string, product?: string): Finish {
  if (chosen === "white" || chosen === "black") return chosen
  if (product && /\bwhite\b/i.test(product)) return "white"
  return h.finish
}
