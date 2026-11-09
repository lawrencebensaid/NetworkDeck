#!/bin/sh
# Creates (or updates) NetworkDeck's self-hosted runner in the k3s cluster: a fresh registration token
# from GitHub (gh, signed in as a repository admin) in the runner-networkdeck-token secret, then
# runner.yaml. A runner that registered before keeps its configuration and ignores the new token.
#
#   KUBECONFIG=<the cluster's kubeconfig> sh deploy/ci-runner/setup.sh
set -eu
here=$(dirname "$0")
ns=github-runners
token=$(gh api -X POST repos/lawrencebensaid/NetworkDeck/actions/runners/registration-token --jq .token)
kubectl -n "$ns" create secret generic runner-networkdeck-token --from-literal=token="$token" \
  --dry-run=client -o yaml | kubectl apply -f -
kubectl apply -f "$here/runner.yaml"
kubectl -n "$ns" rollout status statefulset/runner-networkdeck --timeout=600s
sleep 20
gh api repos/lawrencebensaid/NetworkDeck/actions/runners --jq '.runners[] | "\(.name) \(.status) \([.labels[].name] | join(","))"'
