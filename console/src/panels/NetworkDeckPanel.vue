<script setup lang="ts">
/*
 * The Network Deck itself: the board and chip the firmware runs on, its identity (serial, MAC
 * addresses), what it can do on this hardware, and how it is doing (temperature, memory, uptime).
 */
import { computed, onBeforeUnmount, onMounted } from "vue"

import { Button } from "@/components/ui/button"
import { versionKind, versionLabel } from "@/lib/version"
import { api } from "../session/api.ts"
import { store } from "../session/store.ts"

const emit = defineEmits<{ settings: [section: string] }>()
const y = computed(() => store.system)

/* Faster while the panel is open. */
let timer = 0
async function load() {
  try {
    store.system = await api.system()
  } catch {
    /* offline */
  }
}
onMounted(() => {
  void load()
  timer = window.setInterval(load, 3000)
})
onBeforeUnmount(() => clearInterval(timer))

const mb = (b: number) => (b >= 1024 * 1024 ? `${(b / 1024 / 1024).toFixed(0)} MB` : `${Math.round(b / 1024)} KB`)
function uptime(s: number) {
  const d = Math.floor(s / 86400)
  const h = Math.floor((s % 86400) / 3600)
  const m = Math.floor((s % 3600) / 60)
  return d ? `${d} d ${h} h` : h ? `${h} h ${m} min` : `${m} min`
}
const tempClass = computed(() => {
  const v = y.value
  if (!v || v.temp_c < -100) return ""
  return v.temp_c >= v.hot_c ? "text-status-error-text font-medium" : v.temp_c >= v.warm_c ? "text-status-attention-text font-medium" : ""
})
const features = computed(() => {
  const v = y.value
  if (!v) return []
  return [
    { name: "USB host (Stream Deck)", on: v.usb_host },
    { name: "Ethernet", on: v.ethernet },
    { name: "Matter", on: v.matter },
  ]
})
</script>

<template>
  <div v-if="!y" class="py-6 text-center text-sm text-muted-foreground">Asking the dock…</div>
  <div v-else class="flex flex-col gap-6" data-testid="dock-details">
    <section>
      <h3 class="mb-2 text-xs font-semibold tracking-wide text-muted-foreground uppercase">Hardware</h3>
      <dl class="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1.5 text-sm">
        <template v-if="y.board">
          <dt class="text-muted-foreground">Board</dt>
          <dd>{{ y.board }}</dd>
        </template>
        <dt class="text-muted-foreground">Chip</dt>
        <dd>{{ y.chip }} {{ y.revision }}, {{ y.cores }} {{ y.cores === 1 ? "core" : "cores" }} at {{ y.cpu_mhz }} MHz</dd>
        <dt class="text-muted-foreground">Flash</dt>
        <dd>{{ mb(y.flash_bytes) }}</dd>
        <template v-if="y.psram_bytes">
          <dt class="text-muted-foreground">PSRAM</dt>
          <dd>{{ mb(y.psram_bytes) }}</dd>
        </template>
        <template v-if="y.wifi_via">
          <dt class="text-muted-foreground">Wi-Fi</dt>
          <dd>{{ y.wifi_via }}</dd>
        </template>
      </dl>
      <ul class="mt-3 flex flex-wrap gap-1.5">
        <li
          v-for="f in features"
          :key="f.name"
          class="rounded-full border px-2 py-0.5 text-xs"
          :class="f.on ? 'border-status-on/40 text-status-on-text' : 'text-muted-foreground line-through decoration-muted-foreground/60'"
          :title="f.on ? 'On this hardware' : 'Not on this hardware'"
        >
          {{ f.name }}
        </li>
      </ul>
    </section>

    <section class="border-t pt-5">
      <h3 class="mb-2 text-xs font-semibold tracking-wide text-muted-foreground uppercase">Identity</h3>
      <dl class="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1.5 text-sm">
        <dt class="text-muted-foreground">Serial number</dt>
        <dd class="font-mono text-xs leading-5 select-all">{{ y.serial }}</dd>
        <template v-if="y.mac_ethernet">
          <dt class="text-muted-foreground">Ethernet MAC</dt>
          <dd class="font-mono text-xs leading-5 select-all">{{ y.mac_ethernet }}</dd>
        </template>
        <template v-if="y.mac_wifi && !y.wifi_via">
          <dt class="text-muted-foreground">Wi-Fi MAC</dt>
          <dd class="font-mono text-xs leading-5 select-all">{{ y.mac_wifi }}</dd>
        </template>
        <dt class="text-muted-foreground">Base MAC</dt>
        <dd class="font-mono text-xs leading-5 select-all">{{ y.mac_base }}</dd>
      </dl>
    </section>

    <section class="border-t pt-5">
      <h3 class="mb-2 text-xs font-semibold tracking-wide text-muted-foreground uppercase">Now</h3>
      <dl class="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1.5 text-sm">
        <template v-if="y.temp_c > -100">
          <dt class="text-muted-foreground">Temperature</dt>
          <dd :class="tempClass">{{ y.temp_c.toFixed(1) }} °C</dd>
        </template>
        <dt class="text-muted-foreground">RAM free</dt>
        <dd>{{ mb(y.heap_free) }} of {{ mb(y.heap_total) }} <span class="text-muted-foreground">(lowest {{ mb(y.heap_min) }})</span></dd>
        <template v-if="y.psram_bytes">
          <dt class="text-muted-foreground">PSRAM free</dt>
          <dd>{{ mb(y.psram_free) }}</dd>
        </template>
        <dt class="text-muted-foreground">Up for</dt>
        <dd>{{ uptime(y.uptime) }} <span class="text-muted-foreground">(started after {{ y.reset_reason }})</span></dd>
        <dt class="text-muted-foreground">Firmware</dt>
        <dd><span class="font-mono text-xs">{{ versionLabel(y.firmware) }}</span> <span class="text-muted-foreground">· {{ versionKind(y.firmware) }}, {{ y.partition }}</span></dd>
        <dt class="text-muted-foreground">ESP-IDF</dt>
        <dd class="font-mono text-xs leading-5">{{ y.idf }}</dd>
      </dl>
    </section>

    <div class="border-t pt-5">
      <Button variant="outline" size="sm" @click="emit('settings', 'system')">System settings</Button>
    </div>
  </div>
</template>
