/*
 * A firmware version as people read it: "v0.2.0-pr.4.57", and what kind of build that is. The versions
 * are SemVer (see .github/workflows/firmware.yml): 0.2.0 a release, 0.2.0-beta.N a build of develop,
 * 0.2.0-pr.4.N a build of pull request 4, 0.2.0-dev+<commit> a local build.
 */
export function versionLabel(v: string | undefined) {
  return v ? `v${v.replace(/^v/, "")}` : "–"
}

export function versionKind(v: string | undefined) {
  if (!v) return ""
  const pre = v.split("+")[0]!.split("-").slice(1).join("-")
  if (!pre) return "Release"
  const pr = /^pr\.(\d+)\./.exec(pre)
  if (pr) return `Test build of pull request ${pr[1]}`
  if (pre.startsWith("beta")) return "Beta (the next release, in progress)"
  if (pre.startsWith("dev")) return "Development build"
  return "Pre-release"
}
