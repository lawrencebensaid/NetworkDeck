/*
 * Hand the built console to the firmware: dist/index.html, gzipped, as
 * firmware/components/web/assets/index.html.gz (served with Content-Encoding: gzip).
 * Run by `npm run build:firmware`; the result is committed with the source change.
 */
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { gzipSync } from "node:zlib";

const consoleDir = join(dirname(fileURLToPath(import.meta.url)), "..");
const assets = join(consoleDir, "..", "firmware", "components", "web", "assets");
mkdirSync(assets, { recursive: true });
const page = readFileSync(join(consoleDir, "dist", "index.html"));
/* Level 9, no name or time in the header: the same page always gives the same bytes. */
const packed = gzipSync(page, { level: 9 });
writeFileSync(join(assets, "index.html.gz"), packed);
console.log(`firmware/components/web/assets/index.html.gz: ${page.length} bytes, ${packed.length} gzipped`);
