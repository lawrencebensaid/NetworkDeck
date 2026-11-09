<script setup lang="ts">
/* NetworkDeck's mark: a deck of keys with a network dot. */
defineProps<{ size?: number }>()
</script>

<template>
  <svg :width="size ?? 28" :height="size ?? 28" viewBox="0 0 28 28" fill="none" aria-hidden="true">
    <rect x="1" y="4" width="26" height="20" rx="5" class="fill-foreground" />
    <g class="fill-background">
      <rect x="5" y="8" width="5" height="5" rx="1.2" />
      <rect x="11.5" y="8" width="5" height="5" rx="1.2" />
      <rect x="18" y="8" width="5" height="5" rx="1.2" />
      <rect x="5" y="15" width="5" height="5" rx="1.2" />
      <rect x="11.5" y="15" width="5" height="5" rx="1.2" />
    </g>
    <circle cx="20.5" cy="17.5" r="2.5" class="fill-sky-500" />
  </svg>
</template>
