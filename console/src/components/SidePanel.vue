<script setup lang="ts">
/*
 * The drawer a panel opens in: 340 px beside the rail when wide, a sheet over the page on a phone. It
 * enters and leaves along the same path, from the rail it belongs to.
 */
import { useMediaQuery } from "@vueuse/core"

import { Sheet, SheetContent, SheetDescription, SheetTitle } from "@/components/ui/sheet"
import { WIDE_QUERY } from "../panels/panels.ts"
import PanelHeader from "./PanelHeader.vue"

defineProps<{ open: boolean; title: string }>()
defineEmits<{ close: [] }>()

const wide = useMediaQuery(WIDE_QUERY)
</script>

<template>
  <Sheet v-if="!wide" :open="open" @update:open="(now) => now || $emit('close')">
    <SheetContent side="bottom" :show-close-button="false" class="max-h-[85dvh] gap-0" data-testid="side-panel">
      <PanelHeader @close="$emit('close')">
        <template #title>
          <SheetTitle class="text-[15px] font-semibold">{{ title }}</SheetTitle>
          <SheetDescription class="sr-only">{{ title }}</SheetDescription>
        </template>
      </PanelHeader>
      <div class="min-h-0 flex-1 overflow-y-auto p-4"><slot /></div>
    </SheetContent>
  </Sheet>
  <Transition
    v-else
    enter-active-class="panel-move"
    leave-active-class="panel-move"
    enter-from-class="-translate-x-3 opacity-0"
    leave-to-class="-translate-x-3 opacity-0"
  >
    <aside
      v-if="open"
      :aria-label="title"
      class="relative z-[5] flex w-[340px] shrink-0 flex-col border-r bg-card"
      data-testid="side-panel"
      @keydown.esc="$emit('close')"
    >
      <PanelHeader @close="$emit('close')">
        <template #title>
          <h2 class="text-[15px] font-semibold tracking-tight">{{ title }}</h2>
        </template>
      </PanelHeader>
      <div class="min-h-0 flex-1 overflow-y-auto p-4"><slot /></div>
    </aside>
  </Transition>
</template>
