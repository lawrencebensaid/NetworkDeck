<script setup lang="ts">
/*
 * The Stream Deck on the dock's USB port: what it is, who draws on it now, and what the dock does with
 * it. The brightness is that of the dock's own screens; the Stream Deck app sets its own while it is
 * connected.
 */
import { computed, ref, watch } from "vue"
import { Usb } from "@lucide/vue"
import { toast } from "vue-sonner"

import { Label } from "@/components/ui/label"
import { api } from "../session/api.ts"
import { store } from "../session/store.ts"

const d = computed(() => store.deck)

/* The slider follows the dock until it is moved; a move is sent when it is let go. */
const brightness = ref(70)
const dragging = ref(false)
watch(
  () => d.value?.brightness,
  (b) => {
    if (b !== undefined && !dragging.value) brightness.value = b
  },
  { immediate: true },
)
async function commit() {
  dragging.value = false
  const r = await api.brightness(brightness.value)
  if (!r.ok) toast.error(r.message)
}
</script>

<template>
  <div v-if="!d?.attached" class="flex flex-col items-center gap-3 py-6 text-center text-sm text-muted-foreground">
    <Usb class="size-7" />
    <p class="text-foreground">No Stream Deck plugged in</p>
    <p>Plug a Stream Deck MK.2 or Mini straight into the dock's USB-A port.</p>
  </div>
  <div v-else class="flex flex-col gap-6">
    <dl class="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1.5 text-sm" data-testid="deck-details">
      <dt class="text-muted-foreground">Model</dt>
      <dd>{{ d.model }}</dd>
      <dt class="text-muted-foreground">Keys</dt>
      <dd>{{ d.cols }} × {{ d.rows }}, {{ d.key_px }} px each</dd>
      <template v-if="d.serial">
        <dt class="text-muted-foreground">Serial number</dt>
        <dd class="font-mono text-xs leading-5 select-all">{{ d.serial }}</dd>
      </template>
      <template v-if="d.product">
        <dt class="text-muted-foreground">USB name</dt>
        <dd>{{ d.product }}</dd>
      </template>
      <template v-if="d.usb_rev">
        <dt class="text-muted-foreground">USB revision</dt>
        <dd class="font-mono text-xs leading-5">{{ d.usb_rev }}</dd>
      </template>
      <dt class="text-muted-foreground">Showing</dt>
      <dd>{{ d.source === "app" ? "The Stream Deck app's profile" : "The dock's own screens" }}</dd>
    </dl>

    <div class="flex flex-col gap-2 border-t pt-5">
      <div class="flex items-baseline justify-between">
        <Label for="bright">Brightness of the dock's screens</Label>
        <span class="text-sm tabular-nums text-muted-foreground">{{ brightness }}%</span>
      </div>
      <input
        id="bright"
        v-model.number="brightness"
        type="range"
        min="5"
        max="100"
        step="5"
        class="w-full accent-primary"
        data-testid="deck-brightness"
        @pointerdown="dragging = true"
        @change="commit"
      />
      <p class="text-xs text-muted-foreground">While the Stream Deck app is connected, it sets the brightness itself.</p>
    </div>
  </div>
</template>
