<script setup lang="ts">
/* The bottom line: build, serial, and whether the page still hears from the dock. */
import { useIntervalFn } from "@vueuse/core"
import { computed, ref } from "vue"

import { store } from "../session/store.ts"

const now = ref(Date.now())
useIntervalFn(() => (now.value = Date.now()), 1000)
const stale = computed(() => store.lastSeen > 0 && now.value - store.lastSeen > 5000)
</script>

<template>
  <footer class="hidden h-7 shrink-0 items-center gap-4 border-t bg-card px-3 text-xs text-muted-foreground md:flex">
    <span v-if="stale" class="flex items-center gap-1.5 text-amber-600 dark:text-amber-400" role="status">
      <span class="size-1.5 rounded-full bg-amber-500" /> The dock is not answering
    </span>
    <span v-else class="flex items-center gap-1.5"><span class="size-1.5 rounded-full bg-emerald-500" /> Live</span>
    <span v-if="store.status">Firmware {{ store.status.firmware }} · build {{ store.status.build }}</span>
    <span v-if="store.status" class="ml-auto font-mono">{{ store.status.serial }}</span>
  </footer>
</template>
