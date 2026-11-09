<script setup lang="ts">
/* What the dock says of itself. */
import { store } from "../session/store.ts"
import { versionKind, versionLabel } from "@/lib/version"
</script>

<template>
  <dl class="grid grid-cols-[auto_1fr] gap-x-6 gap-y-2 text-sm">
    <dt class="text-muted-foreground">Name</dt>
    <dd>{{ store.status?.name ?? "–" }}</dd>
    <dt class="text-muted-foreground">Serial number</dt>
    <dd class="font-mono select-all">{{ store.status?.serial ?? "–" }}</dd>
    <dt class="text-muted-foreground">Firmware</dt>
    <dd><span class="font-mono">{{ versionLabel(store.status?.build) }}</span> <span class="text-muted-foreground">· {{ versionKind(store.status?.build) }}</span></dd>
    <dt class="text-muted-foreground">Reported to the app</dt>
    <dd class="font-mono">{{ store.status?.firmware ?? "–" }}</dd>
    <dt class="text-muted-foreground">Board</dt>
    <dd>Waveshare ESP32-P4-NANO</dd>
  </dl>
  <p class="mt-6 text-xs text-muted-foreground">
    NetworkDeck is open source:
    <a class="underline underline-offset-2 hover:text-foreground" href="https://github.com/lawrencebensaid/NetworkDeck" target="_blank" rel="noopener">github.com/lawrencebensaid/NetworkDeck</a>
  </p>
</template>
