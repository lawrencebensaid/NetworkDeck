<script setup lang="ts">
/* The protocol trace: the last frames between the Stream Deck app and the dock. */
import { onBeforeUnmount, onMounted, ref } from "vue"

import { Button } from "@/components/ui/button"
import { api } from "../session/api.ts"

const text = ref("")
let timer = 0

async function load() {
  try {
    text.value = await api.trace()
  } catch {
    /* signed out or offline */
  }
}

onMounted(() => {
  void load()
  timer = window.setInterval(load, 2000)
})
onBeforeUnmount(() => clearInterval(timer))
</script>

<template>
  <div class="flex flex-col gap-3">
    <p class="text-sm text-muted-foreground">The last protocol frames between the Stream Deck app and the dock, newest last.</p>
    <pre class="max-h-[60dvh] overflow-auto rounded-md border bg-muted/40 p-2 font-mono text-[11px] leading-snug whitespace-pre-wrap">{{ text || "Nothing yet." }}</pre>
    <Button variant="outline" size="sm" @click="load">Refresh</Button>
  </div>
</template>
