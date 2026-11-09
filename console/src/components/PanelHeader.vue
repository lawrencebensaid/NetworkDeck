<script setup lang="ts">
/* The head of a panel: its name and Close. */
import { Button } from "@/components/ui/button"

defineEmits<{ close: [] }>()
</script>

<template>
  <div class="flex h-14 shrink-0 items-center gap-2 border-b pr-3 pl-4">
    <slot name="title" />
    <Button type="button" variant="outline" size="sm" class="ml-auto" data-testid="panel-close" @click="$emit('close')">Close</Button>
  </div>
</template>
