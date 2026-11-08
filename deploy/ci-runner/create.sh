#!/bin/sh
# Creates NetworkDeck's self-hosted runner (label networkdeck-ci) in the k3s cluster's github-runners
# namespace, as a copy of the existing showjumpingcalendar runner with its names, labels and
# repository swapped. Prints the diff first; run with --apply to create it.
#
#   KUBECONFIG=<the cluster's kubeconfig> sh deploy/ci-runner/create.sh [--apply]
set -eu
ns=github-runners
src=${SRC:-github-actions-linux-showjumpingcalendar}
out=$(mktemp)
kubectl -n "$ns" get deploy "$src" -o json | python3 -c '
import json, sys
d = json.load(sys.stdin)
for k in ("uid", "resourceVersion", "generation", "creationTimestamp", "managedFields"):
    d["metadata"].pop(k, None)
d.pop("status", None)
d["metadata"].get("annotations", {}).pop("deployment.kubernetes.io/revision", None)
d["metadata"].get("annotations", {}).pop("kubectl.kubernetes.io/last-applied-configuration", None)
s = json.dumps(d)
for a, b in (("showjumpingcalendar", "networkdeck"), ("FEIScheduleReader", "NetworkDeck"), ("ShowjumpingCalendar", "NetworkDeck")):
    s = s.replace(a, b)
print(s)' > "$out"
kubectl -n "$ns" get deploy "$src" -o json | python3 -m json.tool > "$out.src"
python3 -m json.tool "$out" | diff "$out.src" - || true
if [ "${1:-}" = "--apply" ]; then
  kubectl apply -f "$out"
  kubectl -n "$ns" rollout status deploy/github-actions-linux-networkdeck --timeout=180s
  gh api repos/lawrencebensaid/NetworkDeck/actions/runners --jq '.runners[] | "\(.name) \(.status) \([.labels[].name] | join(","))"'
else
  echo; echo "Looks right? Run again with --apply."
fi
