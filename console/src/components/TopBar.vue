<script setup lang="ts">
/*
 * The top bar: the dock (mark, name, and whether the page hears from it) at the left, in a cell as wide
 * as the rail under it; in the middle its lights: the Stream Deck on its USB port, its network and the
 * dock's own hardware (temperature, memory). Each
 * light opens what it knows and where to change it. Translucent, so the stage reads as running under it.
 */
import { computed, ref } from "vue"

import { Button } from "@/components/ui/button"
import { TEXT_CLASS, useLights } from "../session/lights.ts"
import { store } from "../session/store.ts"
import BarLight from "./BarLight.vue"
import BrandMark from "./BrandMark.vue"

const emit = defineEmits<{ settings: [section?: string]; panel: [id: "deck" | "dock"] }>()

const { deck, network, dock, stale } = useLights()
const y = computed(() => store.system)
const dockOpen = ref(false)
function toDock() {
  dockOpen.value = false
  emit("panel", "dock")
}
const s = computed(() => store.status)
const d = computed(() => store.deck)
const deckOpen = ref(false)
const netOpen = ref(false)

const sub = computed(() => {
  if (stale.value) return "Not answering"
  if (!s.value) return "Connecting"
  return s.value.ip && s.value.link !== "No link" ? `${s.value.link} · ${s.value.ip}` : s.value.link
})

function toDeck() {
  deckOpen.value = false
  emit("panel", "deck")
}
function toSettings(section: string) {
  netOpen.value = false
  emit("settings", section)
}
</script>

<template>
  <header class="chrome relative z-10 flex h-12 shrink-0 items-center gap-3 border-b pr-3" data-testid="top-bar">
    <div class="flex w-[52px] shrink-0 justify-center"><BrandMark :size="26" /></div>
    <div class="min-w-0 leading-tight">
      <div class="truncate text-[15px] font-semibold tracking-tight" data-testid="device-name">{{ s?.name ?? "NetworkDeck" }}</div>
      <div class="flex items-center gap-1.5 text-xs text-muted-foreground">
        <span
          class="size-1.5 rounded-full transition-colors duration-500"
          :class="stale ? 'bg-status-error' : s ? 'bg-status-on' : 'bg-status-attention'"
          aria-hidden="true"
        />
        <span class="truncate" :class="stale && TEXT_CLASS.error">{{ sub }}</span>
      </div>
    </div>

    <!-- Centred where there is room; on a phone at the right, out of the name's way. -->
    <div
      class="ml-auto flex shrink-0 items-center gap-1.5 sm:absolute sm:top-1/2 sm:left-1/2 sm:ml-0 sm:-translate-x-1/2 sm:-translate-y-1/2"
      aria-label="Status lights"
      data-testid="lights"
    >
      <BarLight v-model:open="deckOpen" :light="deck">
        <p class="text-muted-foreground">{{ deck.tip }}</p>
        <dl v-if="d?.attached" class="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1">
          <dt class="text-muted-foreground">Model</dt>
          <dd>{{ d.model }}</dd>
          <dt class="text-muted-foreground">Keys</dt>
          <dd>{{ d.cols }} × {{ d.rows }}</dd>
          <template v-if="d.serial">
            <dt class="text-muted-foreground">Serial</dt>
            <dd class="truncate font-mono text-xs leading-5">{{ d.serial }}</dd>
          </template>
        </dl>
        <Button v-if="d?.attached" variant="outline" size="sm" @click="toDeck">Stream Deck details</Button>
      </BarLight>
      <BarLight v-model:open="netOpen" :light="network">
        <p v-if="stale || !s" class="text-muted-foreground">{{ network.tip }}</p>
        <dl v-if="s && !stale" class="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1">
          <dt class="text-muted-foreground">Connection</dt>
          <dd>{{ s.link }}</dd>
          <template v-if="s.link === 'Wi-Fi'">
            <dt class="text-muted-foreground">Network</dt>
            <dd class="truncate">{{ s.wifi_ssid }}</dd>
          </template>
          <dt class="text-muted-foreground">Hostname</dt>
          <dd class="truncate font-mono text-xs leading-5">{{ s.hostname }}.local</dd>
          <dt class="text-muted-foreground">IP address</dt>
          <dd class="font-mono text-xs leading-5">{{ s.ip || "–" }}</dd>
          <dt class="text-muted-foreground">Addressing</dt>
          <dd>{{ s.dhcp ? "DHCP" : "Static" }}</dd>
        </dl>
        <div class="flex gap-2">
          <Button variant="outline" size="sm" @click="toSettings('network')">Network settings</Button>
          <Button v-if="s?.wifi_available" variant="ghost" size="sm" @click="toSettings('wifi')">Wi-Fi</Button>
        </div>
      </BarLight>
      <BarLight v-model:open="dockOpen" :light="dock">
        <p class="text-muted-foreground">{{ dock.tip }}</p>
        <dl v-if="y" class="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1">
          <dt class="text-muted-foreground">Chip</dt>
          <dd>{{ y.chip }} {{ y.revision }}</dd>
          <template v-if="y.temp_c > -100">
            <dt class="text-muted-foreground">Temperature</dt>
            <dd>{{ Math.round(y.temp_c) }} °C</dd>
          </template>
          <dt class="text-muted-foreground">Free RAM</dt>
          <dd>{{ Math.round(y.heap_free / 1024) }} KB</dd>
        </dl>
        <Button variant="outline" size="sm" @click="toDock">Network Deck details</Button>
      </BarLight>
    </div>
  </header>
</template>
