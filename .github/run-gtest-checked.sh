#!/bin/bash
# Runs a gtest binary with a filter, after checking the filter against the tests the binary carries.
# A filter that selects nothing still exits 0, so a build missing the suites a CI job names would
# report green having run no test, and one missing only some of them would quietly run fewer. An
# exclusion that excludes nothing is just as stale. So every positive pattern must select a test,
# every negative pattern must take one out of the selection, and something must be left to run, or the
# script fails before running.
#
# Usage: run-gtest-checked.sh <gtest binary> <gtest filter>
set -euo pipefail

[[ $# -eq 2 ]] || { echo "usage: $0 <gtest binary> <gtest filter>" >&2; exit 2; }
binary=$1
filter=$2

# Test names cannot contain '-', so the first one splits the filter into its two halves.
positive=${filter%%-*}
negative=""
[[ $filter == *-* ]] && negative=${filter#*-}
# gtest reads an empty positive half as "every test".
[[ -n $positive ]] || positive='*'

# The listing indents each test under its suite line.
count_tests() {
    local listing
    listing=$("$binary" --gtest_filter="$1" --gtest_list_tests) || exit
    grep -c '^  ' <<< "$listing" || true
}

problems=()
IFS=: read -ra patterns <<< "$positive"
for pattern in "${patterns[@]}"; do
    selected=$(count_tests "$pattern")
    echo "$pattern selects $selected test(s)"
    (( selected > 0 )) || problems+=("$pattern selects no test")
done

selected=$(count_tests "$positive")
IFS=: read -ra patterns <<< "$negative"
for pattern in "${patterns[@]}"; do
    remaining=$(count_tests "$positive-$pattern")
    excluded=$(( selected - remaining ))
    echo "-$pattern excludes $excluded test(s)"
    (( excluded > 0 )) || problems+=("-$pattern excludes no test")
done
# Each negative pattern can exclude something while together they exclude everything.
total=$(count_tests "$filter")
(( total > 0 )) || problems+=("the filter as a whole selects no test")

if (( ${#problems[@]} > 0 )); then
    printf "%s: filter '%s' does not match this build:\n" "$binary" "$filter" >&2
    printf '  %s\n' "${problems[@]}" >&2
    exit 1
fi
exec "$binary" --gtest_filter="$filter"
