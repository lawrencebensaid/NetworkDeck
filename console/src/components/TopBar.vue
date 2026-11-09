<script setup lang="ts">
/*
 * The top bar: the dock (mark, name, how its network is doing) at the left; in the middle what it is
 * serving: the Stream Deck app's connection and the deck on its USB port. Translucent, so the stage
 * reads as running underneath.
 */
import { computed } from "vue"

import { store } from "../session/store.ts"
import BrandMark from "./BrandMark.vue"

const s = computed(() => store.status)
const online = computed(() => !!s.value?.ip && s.value.link !== "No link")
</script>

<template>
  <header
    class="chrome relative z-10 flex h-12 shrink-0 items-center gap-3 border-b pr-3"
    data-testid="top-bar"
  >
    <div class="flex w-[52px] shrink-0 justify-center"><BrandMark :size="26" /></div>
    <div class="min-w-0 leading-tight">
      <div class="truncate text-[15px] font-semibold tracking-tight">{{ s?.name ?? "NetworkDeck" }}</div>
      <div class="flex items-center gap-1.5 text-xs text-muted-foreground">
        <span class="size-2 rounded-full" :class="online ? 'bg-emerald-500' : 'bg-amber-500'" aria-hidden="true" />
        <span class="truncate">{{ online ? `${s?.link} · ${s?.ip}` : s?.link ?? "Connecting" }}</span>
      </div>
    </div>
    <div class="ml-auto hidden items-center gap-2 sm:absolute sm:top-1/2 sm:left-1/2 sm:ml-0 sm:flex sm:-translate-x-1/2 sm:-translate-y-1/2">
      <span class="pill" :data-on="s?.host">
        <span class="size-1.5 rounded-full" :class="s?.host ? 'bg-emerald-500' : 'bg-muted-foreground/50'" />
        {{ s?.host ? "Stream Deck app connected" : "Stream Deck app not connected" }}
      </span>
      <span class="pill" :data-on="!!s?.deck">
        <span class="size-1.5 rounded-full" :class="s?.deck ? 'bg-emerald-500' : 'bg-muted-foreground/50'" />
        {{ s?.deck || "No deck plugged in" }}
      </span>
    </div>
  </header>
</template>

<style scoped>
.pill {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  height: 26px;
  padding: 0 10px;
  border-radius: 999px;
  border: 1px solid var(--border);
  font-size: 12px;
  color: var(--muted-foreground);
  background: color-mix(in oklab, var(--card) 70%, transparent);
}
.pill[data-on="true"] {
  color: var(--foreground);
}
</style>
