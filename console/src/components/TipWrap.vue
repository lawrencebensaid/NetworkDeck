<script setup lang="ts">
/*
 * A tooltip for a menu button. Reka's tooltip and menu each position
 * themselves from the nearest popper above their trigger, so a menu trigger
 * inside a tooltip trigger would anchor its menu to the tooltip. The menu goes
 * inside this wrapper instead: the wrapper is what the tooltip points at, the
 * button inside it is what opens the menu. `quiet` keeps the tooltip away
 * while the menu (or popover) is open, so it never sits over its own content.
 */
import { Tooltip, TooltipContent, TooltipTrigger } from "@/components/ui/tooltip"

defineProps<{
  tip: string
  quiet?: boolean
  /** Which side of the button the tooltip takes; above it unless told. */
  side?: "top" | "right" | "bottom" | "left"
  /** The wrapper fills the width it is given, for something that does. */
  block?: boolean
}>()
</script>

<template>
  <Tooltip :disabled="quiet">
    <TooltipTrigger as-child>
      <span :class="block ? 'flex rounded-md' : 'inline-flex rounded-md'"><slot /></span>
    </TooltipTrigger>
    <TooltipContent :side="side" :side-offset="6">{{ tip }}</TooltipContent>
  </Tooltip>
</template>
