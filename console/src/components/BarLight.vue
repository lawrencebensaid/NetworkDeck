<script setup lang="ts">
/*
 * One light of the top bar: the icon of what it reports on, in the colour of its state with a faint
 * glow, so it reads as an LED; a change of state fades over, an error blinks, attention breathes. It is
 * a button: it opens a popover headed by the light's name and state, with what the caller puts in it.
 */
import { computed, ref, watch } from "vue"
import { X } from "@lucide/vue"
import { PopoverClose } from "reka-ui"

import { Button } from "@/components/ui/button"
import { Popover, PopoverContent, PopoverTrigger } from "@/components/ui/popover"
import { Tooltip, TooltipContent, TooltipTrigger } from "@/components/ui/tooltip"
import { cn } from "@/lib/utils"
import { GLOW_CLASS, ICON_CLASS, TEXT_CLASS, WASH_CLASS, type Light } from "../session/lights.ts"

const props = defineProps<{ light: Light }>()
const open = defineModel<boolean>("open", { default: false })

const label = computed(() => `${props.light.name}: ${props.light.state}. ${props.light.tip}`)

/* A blinking or breathing light starts its cycle where its state arrived, not in step with the other. */
const phase = ref("0s")
watch(
  () => props.light.tone,
  () => (phase.value = `-${(Math.random() * 1.4).toFixed(2)}s`),
  { immediate: true },
)
const beat = computed(() => (props.light.tone === "error" ? "led-blink" : props.light.tone === "attention" ? "led-pulse" : ""))
</script>

<template>
  <Tooltip :disabled="open">
    <TooltipTrigger as-child>
      <span class="inline-flex rounded-md">
        <Popover v-model:open="open">
          <PopoverTrigger as-child>
            <button
              type="button"
              :aria-label="label"
              :data-light="light.id"
              :data-tone="light.tone"
              :class="cn('inline-flex size-7 items-center justify-center rounded-md outline-none transition-colors focus-visible:ring-3 focus-visible:ring-ring/50', WASH_CLASS[light.tone])"
            >
              <component
                :is="light.icon"
                :class="cn('size-[17px] transition-[color,filter] duration-500', ICON_CLASS[light.tone], GLOW_CLASS[light.tone], beat)"
                :style="{ '--led-phase': phase }"
              />
            </button>
          </PopoverTrigger>
          <PopoverContent side="bottom" align="center" :side-offset="8" class="w-[300px] max-w-[calc(100vw-16px)] gap-0 p-0" :data-testid="`light-panel-${light.id}`">
            <div class="flex h-10 items-center justify-between border-b pr-1.5 pl-3">
              <div class="flex min-w-0 items-center gap-2">
                <component :is="light.icon" :class="cn('size-4 shrink-0', ICON_CLASS[light.tone])" aria-hidden="true" />
                <h3 class="text-xs font-semibold tracking-[0.08em] text-muted-foreground uppercase">{{ light.name }}</h3>
                <span :class="cn('truncate text-xs font-medium', TEXT_CLASS[light.tone])" data-testid="light-state">{{ light.state }}</span>
              </div>
              <PopoverClose as-child>
                <Button type="button" variant="ghost" size="icon-xs" aria-label="Close"><X /></Button>
              </PopoverClose>
            </div>
            <div class="flex flex-col gap-3 p-3 text-sm">
              <slot><p class="text-muted-foreground">{{ light.tip }}</p></slot>
            </div>
          </PopoverContent>
        </Popover>
      </span>
    </TooltipTrigger>
    <TooltipContent side="bottom" :side-offset="6" class="max-w-72">
      <span class="grid gap-0.5">
        <span class="font-medium">{{ light.name }} <span class="opacity-60">&middot;</span> {{ light.state }}</span>
        <span class="opacity-75">{{ light.tip }}</span>
      </span>
    </TooltipContent>
  </Tooltip>
</template>
