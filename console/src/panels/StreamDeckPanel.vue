<script setup lang="ts">
/*
 * The Stream Deck on the dock's USB port: what it is, who draws on it now, and what the dock does with
 * it. The screen brightness is the deck's while it shows the dock's own screens; the Stream Deck app
 * sets its own while it is connected.
 */
import { computed, ref, watch } from "vue"
import { Usb } from "@lucide/vue"
import { toast } from "vue-sonner"

import { Label } from "@/components/ui/label"
import { api } from "../session/api.ts"
import { cn } from "@/lib/utils"
import { finishOf, hardware, type Finish } from "../session/models.ts"
import { refreshDeck, store } from "../session/store.ts"

const d = computed(() => store.deck)
const hw = computed(() => hardware(d.value?.pid, d.value?.cols, d.value?.rows, d.value?.model))

const finish = computed(() => finishOf(hw.value, d.value?.finish, d.value?.serial))
async function setFinish(f: Finish) {
  const r = await api.finish(f)
  if (!r.ok) toast.error(r.message)
  else refreshDeck()
}
const choice = (on: boolean) =>
  cn(
    "flex flex-1 items-center justify-center gap-2 rounded-md border px-3 py-1.5 text-sm transition-colors outline-none focus-visible:ring-3 focus-visible:ring-ring/50",
    on ? "border-primary bg-primary/5 font-medium" : "text-muted-foreground hover:bg-muted/60 hover:text-foreground",
  )

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
    <p>Plug a Stream Deck straight into the dock's USB-A port.</p>
  </div>
  <div v-else class="flex flex-col gap-6">
    <dl class="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1.5 text-sm" data-testid="deck-details">
      <dt class="text-muted-foreground">Model</dt>
      <dd>{{ d.model ?? hw.name }}</dd>
      <template v-if="hw.cols">
        <dt class="text-muted-foreground">Keys</dt>
        <dd>{{ hw.cols }} × {{ hw.rows }}<template v-if="d.key_px">, {{ d.key_px }} px each</template></dd>
      </template>
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
      <dd>{{ d.live === false ? "Nothing here: this model has no live view yet" : d.source === "app" ? "The Stream Deck app's profile" : "The dock's own screens" }}</dd>
    </dl>

    <div class="flex flex-col gap-2 border-t pt-5">
      <span class="text-sm font-medium" id="finish-label">Finish</span>
      <div class="flex gap-2" role="radiogroup" aria-labelledby="finish-label">
        <button v-for="f in (['black', 'white'] as const)" :key="f" type="button" role="radio" :aria-checked="finish === f" :data-finish="f" :class="choice(finish === f)" @click="setFinish(f)">
          <span :class="cn('size-3.5 rounded-full ring-1 ring-black/20', f === 'white' ? 'bg-[#f2f1ed]' : 'bg-zinc-800')" aria-hidden="true" />
          {{ f === "black" ? "Black" : "White" }}
        </button>
      </div>
      <p class="text-xs text-muted-foreground">Neither USB nor the deck says its colour, so the dock remembers it for this deck.</p>
    </div>

    <div v-if="d.live !== false" class="flex flex-col gap-2 border-t pt-5">
      <div class="flex items-baseline justify-between">
        <Label for="bright">Screen brightness</Label>
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
      <p class="text-xs text-muted-foreground">While the Stream Deck app is connected, the app sets the brightness.</p>
    </div>
  </div>
</template>
