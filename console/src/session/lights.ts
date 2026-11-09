/*
 * The top bar's lights: the Stream Deck on the dock's USB port, and the dock's own network. Each is a
 * tone, a word for its state and a sentence for its tooltip, from what the page polls. The network
 * light also says when the page stops hearing from the dock.
 */
import { useIntervalFn } from "@vueuse/core"
import { EthernetPort, RadioTower, Unplug, Wifi, WifiOff } from "@lucide/vue"
import { computed, ref, type Component } from "vue"

import DeckIcon from "../components/DeckIcon.vue"
import { hardware } from "./models.ts"
import { store } from "./store.ts"

export type Tone = "on" | "attention" | "error" | "busy" | "off"

export interface Light {
  id: "deck" | "network"
  name: string
  tone: Tone
  state: string
  tip: string
  icon: Component
}

/* Spelled out so Tailwind finds them. The icon is the light: it takes the tone's colour and a glow. */
export const ICON_CLASS: Record<Tone, string> = {
  on: "text-status-on",
  attention: "text-status-attention",
  error: "text-status-error",
  busy: "text-status-busy",
  off: "text-status-off opacity-70",
}
export const GLOW_CLASS: Record<Tone, string> = {
  on: "drop-shadow-[0_0_4px_var(--status-on)]",
  attention: "drop-shadow-[0_0_4px_var(--status-attention)]",
  error: "drop-shadow-[0_0_4px_var(--status-error)]",
  busy: "drop-shadow-[0_0_4px_var(--status-busy)]",
  off: "",
}
export const TEXT_CLASS: Record<Tone, string> = {
  on: "text-status-on-text",
  attention: "text-status-attention-text",
  error: "text-status-error-text",
  busy: "text-status-busy-text",
  off: "text-muted-foreground",
}
export const WASH_CLASS: Record<Tone, string> = {
  on: "hover:bg-status-on/10 aria-expanded:bg-status-on/15",
  attention: "hover:bg-status-attention/10 aria-expanded:bg-status-attention/15",
  error: "hover:bg-status-error/10 aria-expanded:bg-status-error/15",
  busy: "hover:bg-status-busy/10 aria-expanded:bg-status-busy/15",
  off: "hover:bg-muted aria-expanded:bg-muted",
}

/* The page has not heard from the dock for this long: it says so. */
const STALE_MS = 5000

export function useLights() {
  const now = ref(Date.now())
  useIntervalFn(() => (now.value = Date.now()), 1000)
  const stale = computed(() => store.lastSeen > 0 && now.value - store.lastSeen > STALE_MS)

  const deck = computed<Light>(() => {
    const d = store.deck
    const base = { id: "deck" as const, name: "Stream Deck", icon: DeckIcon }
    if (!d) return { ...base, tone: "off", state: "Checking", tip: "Asking the dock about its deck." }
    if (!d.attached)
      return { ...base, icon: Unplug, tone: "off", state: "Not plugged in", tip: "Plug a Stream Deck into the dock's USB-A port." }
    const model = d.model ?? hardware(d.pid).name
    if (store.status?.host) {
      const who = store.status.host_name || store.status.host_ip
      const from = who ? ` on ${who}` : ""
      return { ...base, tone: "on", state: "Stream Deck app", tip: `${model}: the Stream Deck app${from} draws the keys.` }
    }
    return { ...base, tone: "busy", state: "Dock screens", tip: `${model}: no Stream Deck app is connected, so it shows the dock's own screens.` }
  })

  const network = computed<Light>(() => {
    const s = store.status
    const base = { id: "network" as const, name: "Network" }
    if (stale.value)
      return { ...base, icon: WifiOff, tone: "error", state: "Not answering", tip: "The page has not heard from the dock for a few seconds." }
    if (!s) return { ...base, icon: EthernetPort, tone: "off", state: "Checking", tip: "Asking the dock about its network." }
    const how = s.dhcp ? "DHCP" : "static address"
    switch (s.link) {
      case "Ethernet":
        return { ...base, icon: EthernetPort, tone: "on", state: "Ethernet", tip: `${s.ip}, ${how}.` }
      case "Wi-Fi":
        return { ...base, icon: Wifi, tone: "on", state: "Wi-Fi", tip: `${s.wifi_ssid}: ${s.ip}, ${how}.` }
      case "Hotspot":
        return { ...base, icon: RadioTower, tone: "attention", state: "Hotspot", tip: "No network to join: the dock runs its setup hotspot." }
      default:
        return { ...base, icon: WifiOff, tone: "error", state: "No link", tip: "No cable and no Wi-Fi." }
    }
  })

  return { deck, network, stale }
}
