#!/usr/bin/env bash
# repro-ci.sh — trigger the tt-umd CI "Build and run all tests for multiple build types"
# workflow (the same run you'd start from the Actions UI), choosing branch, build type(s),
# how many times to repeat each card, and which cards to run.
set -euo pipefail

REPO="tenstorrent/tt-umd"
WORKFLOW="build-and-run-all-tests-multiple-builds.yml"
REUSABLE="build-and-run-all-tests.yml"   # holds the cards job + build-type options

BRANCH="" BUILD="" REPEAT="" CARDS="" REPO_PATH=""
DO_LIST=0 DRY=0 FORCE=0

usage() {
  cat <<'EOF'
repro-ci.sh — start a CI run (same as the GitHub Actions "Run workflow" button).

USAGE
  repro-ci.sh [--branch B] [--repeat N] [--cards LIST] [--build LIST]
  repro-ci.sh --list          # show the build types and cards this workflow accepts
  repro-ci.sh --dry-run ...    # print the gh command instead of running it

PARAMETERS
  -b, --branch B     Branch to run the workflow from ("Use workflow from" in the UI).
                     Default: the current git branch.
  -r, --repeat N     Times to repeat each card, each repeat its own job. Default: 1
                     (the workflow's default). cards x N must be <= 256.
      --cards LIST   Comma-separated EXACT card names to run (e.g. from --list). Maps to the
                     cards-filter input. Default: omitted -> all cards.
      --build LIST   Comma-separated build types (e.g. Release,ASan). Maps to build-types.
                     Default: omitted -> the workflow's default build set.
      --repo O/R     GitHub repo. Default: tenstorrent/tt-umd.
      --repo-path P  Path to the local repo (to read the workflow for --list/validation).
                     Default: the git repo containing your current directory.
  -l, --list         Parse the workflow and list valid build types and card names, then exit.
      --force        Dispatch even if --cards/--build contain values not in --list.
      --dry-run      Print the gh command that would run, without dispatching.
  -h, --help         This help.

EXAMPLES
  # 50x on 4 cards, Release only, on your branch:
  repro-ci.sh --repeat 50 --build Release \
    --cards tt-ubuntu-2204-n150-stable,tt-ubuntu-2204-n300-stable,tt-ubuntu-2204-p100a-viommu-stable,tt-ubuntu-2204-p150b-stable

  # See what you can pass:
  repro-ci.sh --list

  # A normal single run of everything on main (same as the plain UI defaults):
  repro-ci.sh --branch main

NOTES
  - Needs `gh` authed to dispatch; without it, the command + UI steps are printed instead.
  - --cards / --build are validated against --list; an unknown value aborts before dispatch
    (override with --force). The workflow also rejects an empty match or a >256 matrix.
EOF
}

die() { echo "error: $1" >&2; exit 2; }

# Resolve the local repo path (for reading the workflow files).
resolve_repo_path() {
  [ -n "$REPO_PATH" ] && { echo "$REPO_PATH"; return; }
  git rev-parse --show-toplevel 2>/dev/null || true
}

# Parse the workflow file: `list_from cards` or `list_from builds`.
list_from() {
  local what="$1" root wf
  root="$(resolve_repo_path)"
  [ -n "$root" ] && wf="$root/.github/workflows/$REUSABLE" || wf=""
  [ -n "$wf" ] && [ -f "$wf" ] || return 1
  python3 - "$wf" "$what" <<'PY'
import sys, re, yaml
wf, what = sys.argv[1], sys.argv[2]
d = yaml.safe_load(open(wf))
onk = True if True in d else "on"          # PyYAML parses `on:` as boolean True
if what == "builds":
    print("\n".join(d[onk]["workflow_dispatch"]["inputs"]["build-type"]["options"]))
else:
    run = "\n".join(s.get("run", "") for s in d["jobs"]["cards"]["steps"])
    run = "\n".join(l for l in run.splitlines() if not l.strip().startswith("#"))
    out = []
    for m in re.finditer(r'"card"\s*:\s*"([^"]+)"', run):
        if m.group(1) not in out:
            out.append(m.group(1))
    print("\n".join(out))
PY
}

# Comma list -> JSON array string, e.g. "Release,ASan" -> ["Release","ASan"].
to_json_array() {
  python3 -c 'import json,sys; print(json.dumps([x.strip() for x in sys.argv[1].split(",") if x.strip()]))' "$1"
}

# Reject values not present in the workflow's known list (best-effort; aborts unless --force).
require_known() {
  local label="$1" csv="$2" known="$3" tok bad=0
  [ -n "$known" ] || return 0   # couldn't read the workflow; skip validation
  IFS=',' read -ra toks <<< "$csv"
  for tok in "${toks[@]}"; do
    tok="$(echo "$tok" | xargs)"; [ -n "$tok" ] || continue
    grep -qxF "$tok" <<< "$known" || { echo "error: unknown $label '$tok' — run --list to see valid values" >&2; bad=1; }
  done
  if [ "$bad" -eq 1 ] && [ "$FORCE" -ne 1 ]; then
    die "aborting before dispatch (use --force to override)"
  fi
}

# ---- arg parsing (supports "--flag value" and "--flag=value") ----
while [ $# -gt 0 ]; do
  case "$1" in
    -b|--branch|--ref)               BRANCH="$2"; shift 2 ;;
    --branch=*|--ref=*)              BRANCH="${1#*=}"; shift ;;
    -r|--repeat)                     REPEAT="$2"; shift 2 ;;
    --repeat=*)                      REPEAT="${1#*=}"; shift ;;
    --cards|--card-to-run)           CARDS="$2"; shift 2 ;;
    --cards=*|--card-to-run=*)       CARDS="${1#*=}"; shift ;;
    --build|--build-type|--build-types)     BUILD="$2"; shift 2 ;;
    --build=*|--build-type=*|--build-types=*) BUILD="${1#*=}"; shift ;;
    --repo)                          REPO="$2"; shift 2 ;;
    --repo=*)                        REPO="${1#*=}"; shift ;;
    --repo-path)                     REPO_PATH="$2"; shift 2 ;;
    --repo-path=*)                   REPO_PATH="${1#*=}"; shift ;;
    -l|--list)                       DO_LIST=1; shift ;;
    --force)                         FORCE=1; shift ;;
    --dry-run)                       DRY=1; shift ;;
    -h|--help)                       usage; exit 0 ;;
    *) echo "unknown arg: $1 (try --help)" >&2; exit 2 ;;
  esac
done

if [ "$DO_LIST" -eq 1 ]; then
  echo "Build types:"; list_from builds | sed 's/^/  /' || die "could not read $REUSABLE (run inside the repo or pass --repo-path)"
  echo "Cards:";       list_from cards  | sed 's/^/  /' || die "could not read $REUSABLE (run inside the repo or pass --repo-path)"
  exit 0
fi

# Default branch = current git branch.
[ -n "$BRANCH" ] || BRANCH="$(git rev-parse --abbrev-ref HEAD 2>/dev/null || true)"
[ -n "$BRANCH" ] || die "--branch is required (not inside a git repo)"
[ -z "$REPEAT" ] || case "$REPEAT" in *[!0-9]*) die "--repeat must be a positive integer" ;; esac

# Validate cards/builds against the workflow (best-effort; workflow enforces the hard rules).
[ -n "$CARDS" ] && require_known "card"       "$CARDS" "$(list_from cards  2>/dev/null || true)"
[ -n "$BUILD" ] && require_known "build type" "$BUILD" "$(list_from builds 2>/dev/null || true)"

# Build the -f inputs only for values actually provided (unset -> the workflow's own default).
FARGS=()
[ -n "$REPEAT" ] && FARGS+=(-f "repeat-count=$REPEAT")
[ -n "$CARDS"  ] && FARGS+=(-f "cards-filter=$(to_json_array "$CARDS")")
[ -n "$BUILD"  ] && FARGS+=(-f "build-types=$(to_json_array "$BUILD")")

echo "repo=$REPO  branch=$BRANCH  repeat=${REPEAT:-<default>}  build=${BUILD:-<default>}  cards=${CARDS:-<all>}"

if [ "$DRY" -eq 1 ] || ! command -v gh >/dev/null 2>&1; then
  [ "$DRY" -eq 1 ] || echo "(gh not found — showing the command / UI steps instead)"
  printf 'gh workflow run %s --repo %s --ref %s' "$WORKFLOW" "$REPO" "$BRANCH"
  for a in "${FARGS[@]}"; do printf ' %q' "$a"; done; printf '\n'
  echo "UI: Actions -> Build and run all tests for multiple build types -> Run workflow"
  echo "    Use workflow from: $BRANCH; repeat-count=${REPEAT:-1}; cards-filter=$( [ -n "$CARDS" ] && to_json_array "$CARDS" || echo '(empty=all)')"
  exit 0
fi

gh workflow run "$WORKFLOW" --repo "$REPO" --ref "$BRANCH" "${FARGS[@]}"
echo "Dispatched. Recent runs:"
gh run list --repo "$REPO" --workflow "$WORKFLOW" --limit 3 2>/dev/null || true
