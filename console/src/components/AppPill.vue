<script setup lang="ts">
/*
 * Above the deck: which computer's Stream Deck app is connected, as a glowing pill (its name, and how
 * long it has been connected in the tooltip). Hidden while no app is connected.
 */
import { computed } from "vue"
import { Laptop } from "@lucide/vue"

import { store } from "../session/store.ts"
import TipWrap from "./TipWrap.vue"

function duration(s: number) {
  if (s < 60) return "just now"
  const m = Math.floor(s / 60)
  if (m < 60) return `for ${m} min`
  const h = Math.floor(m / 60)
  return h < 24 ? `for ${h} h ${m % 60} min` : `for ${Math.floor(h / 24)} days`
}

const app = computed(() => {
  const s = store.status
  if (!s?.host || !s.host_ip) return null
  return { name: s.host_name || s.host_ip, ip: s.host_ip, since: duration(s.host_seconds ?? 0) }
})
</script>

<template>
  <TipWrap v-if="app" :tip="`The Stream Deck app on ${app.name} (${app.ip}), connected ${app.since}`" side="bottom">
    <div
      class="flex items-center gap-2 rounded-full border border-status-on/40 bg-status-on/10 py-1 pr-3 pl-2 text-xs font-medium text-status-on-text shadow-[0_0_12px_-2px_var(--status-on)] backdrop-blur"
      role="status"
      data-testid="app-pill"
    >
      <span class="relative flex size-2">
        <span class="absolute inline-flex size-full animate-ping rounded-full bg-status-on opacity-50 motion-reduce:hidden" />
        <span class="relative inline-flex size-2 rounded-full bg-status-on" />
      </span>
      <Laptop class="size-3.5" />
      <span>{{ app.name }}</span>
    </div>
  </TipWrap>
</template>
