#!/bin/sh

# A couple of notes about the command below:
# If we omitted \( and \), the -not filter would've been applied only on -name '*.h', since it has
# higher precedence than -or.
# -print0 will separate each file will null character when printing the result.
# -z in sort tells it to expect null separator between items. Items are sorted just for prettier log.
# -0 in xargs tells it to expect null separator between items, each passed to clang-format.
# -t will print out commands being executed, for prettier log and easier debug.
# -n1 limits the number of items to pass to clang-format to 1. This is also just for easier debugging,
# since clang-format can also accept multiple files.
#
echo "Running clang-format version $(clang-format --version) on all files in the repository."
find . \( -name '*.cpp' -or -name '*.h' -or -name '*.hpp' \) \
    -not -path "./.cpmcache/*" \
    -not -path "./build/*" -print0 | \
    sort -z | \
    xargs -0 -t -n1 clang-format -i -style=file
