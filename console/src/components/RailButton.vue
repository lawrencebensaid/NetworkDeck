<script setup lang="ts">
/*
 * A button of the rail: 36 px square, an icon in it, and a filled look with a
 * stronger border while what it opens is open (a panel it toggles, or a
 * popover or menu it triggers). Its name and tooltip are the caller's:
 * wrap it in a TipWrap.
 */
import type { HTMLAttributes } from "vue"

import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"

defineProps<{ class?: HTMLAttributes["class"] }>()
</script>

<template>
  <Button
    type="button"
    variant="ghost"
    size="icon"
    :class="cn('border-transparent text-muted-foreground hover:text-foreground [&_svg:not([class*=size-])]:size-[18px] aria-expanded:border-input aria-pressed:border-input aria-pressed:bg-muted aria-pressed:text-foreground', $props.class)"
  >
    <slot />
  </Button>
</template>
