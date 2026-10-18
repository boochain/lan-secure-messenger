#!/usr/bin/env bash
# Re-import the latest boochain/lan-secure-messenger-xp into editions/portable (git subtree, full history),
# then regenerate .github/workflows/portable.yml from the imported xp.yml.
#
#   tools/sync-from-xp.sh                  # pull xp main
#   tools/sync-from-xp.sh main ui/relay-ui # pull several xp branches, in order
#   XP_URL=/path/to/local/clone tools/sync-from-xp.sh
#
# Run on unify/v0.6 (or the branch that took over from it) with a clean work tree. Conflicts stop the script
# in the usual git merge state: resolve, `git commit`, then run it again for the remaining refs.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
URL=${XP_URL:-https://github.com/boochain/lan-secure-messenger-xp.git}
PREFIX=editions/portable
[ -z "$(git status --porcelain --untracked-files=no)" ] || { echo "work tree not clean; commit or stash first" >&2; exit 1; }
br=$(git rev-parse --abbrev-ref HEAD)
case "$br" in unify/*|main) ;; *) echo "warning: on branch '$br' (expected unify/v0.6)" >&2;; esac
[ $# -gt 0 ] || set -- main
for ref in "$@"; do
  echo "== pulling $URL $ref into $PREFIX"
  git fetch -q "$URL" "$ref"
  git subtree pull --prefix="$PREFIX" "$URL" "$ref" -m "sync: lan-secure-messenger-xp $ref ($(git rev-parse --short FETCH_HEAD)) into $PREFIX"
done
python3 tools/gen-portable-workflow.py
if ! git diff --quiet -- .github/workflows/portable.yml; then
  git add .github/workflows/portable.yml
  git commit -q -m "ci: regenerate portable.yml from the synced xp.yml"
  echo "portable.yml regenerated"
fi
git log --oneline -5
