/*
 * The build's last step: fold the scripts and styles into the page.
 *
 * The firmware embeds the console as a single gzipped HTML document and serves
 * it from one route, so the build must not leave a script or a stylesheet
 * beside the page. Vite writes them as separate files; this plugin moves them
 * into the page at the end of the build and drops the originals.
 */
import type { Plugin } from "vite";

export interface PageParts {
  /** Published file name (as the page refers to it) to its code. */
  scripts: Map<string, string>;
  styles: Map<string, string>;
}

/* A script may contain "</script>" inside a string; in an inline element that
   would end the element early. Breaking the sequence keeps the code the same. */
function safeScript(code: string): string {
  return code.replace(/<\/script/gi, "<\\/script");
}

function safeStyle(css: string): string {
  return css.replace(/<\/style/gi, "<\\/style");
}

function escapeForPattern(text: string): string {
  return text.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

/**
 * Put each referenced script and stylesheet into the page itself.
 * Returns the new page and the names that were taken in; anything the page
 * does not reference is left alone.
 */
export function inlineParts(html: string, parts: PageParts): { html: string; used: string[] } {
  let page = html;
  const used: string[] = [];

  for (const [name, code] of parts.scripts) {
    const tag = new RegExp(
      `<script([^>]*?)\\ssrc="[^"]*?${escapeForPattern(name)}"([^>]*)></script>`,
    );
    const match = tag.exec(page);
    if (!match) continue;
    /* Keep type="module" and the like; drop what only made sense for a URL. */
    const attrs = `${match[1]}${match[2]}`.replace(/\s+crossorigin(="[^"]*")?/g, "");
    const inline = `<script${attrs}>${safeScript(code)}</script>`;
    page = page.slice(0, match.index) + inline + page.slice(match.index + match[0].length);
    used.push(name);
  }

  for (const [name, css] of parts.styles) {
    const tag = new RegExp(`<link[^>]*?href="[^"]*?${escapeForPattern(name)}"[^>]*>`);
    const match = tag.exec(page);
    if (!match) continue;
    const inline = `<style>${safeStyle(css)}</style>`;
    page = page.slice(0, match.index) + inline + page.slice(match.index + match[0].length);
    used.push(name);
  }

  return { html: page, used };
}

export function singleFile(): Plugin {
  return {
    name: "ipk32-single-file",
    apply: "build",
    enforce: "post",
    generateBundle(_options, bundle) {
      const parts: PageParts = { scripts: new Map(), styles: new Map() };
      for (const file of Object.values(bundle)) {
        if (file.type === "chunk") {
          parts.scripts.set(file.fileName, file.code);
        } else if (file.fileName.endsWith(".css")) {
          parts.styles.set(file.fileName, String(file.source));
        }
      }
      for (const file of Object.values(bundle)) {
        if (file.type !== "asset" || !file.fileName.endsWith(".html")) continue;
        const { html, used } = inlineParts(String(file.source), parts);
        file.source = html;
        for (const name of used) delete bundle[name];
      }
    },
  };
}
