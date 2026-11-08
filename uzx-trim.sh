#!/bin/sh
# Removes everything uZX does not build: its CMakeLists.txt compiles zxtune's AY players only.
# Rerun after merging an upstream tag (see UZX.md); it only deletes, so it is safe to repeat.
set -e
cd "$(dirname "$0")"
git ls-files \
    | grep -vE '^(include/|src/|regression/[^/]+$|3rdparty/fmt/(include|src)/|3rdparty/fmt/LICENSE)' \
    | grep -vxE '(LICENSE|README|UZX)\.md|uzx-trim\.sh|CMakeLists\.txt|\.clang-format|\.gitignore|\.gitattributes' \
    | git --literal-pathspecs rm -q --ignore-unmatch --pathspec-from-file=-
git rm -rq --ignore-unmatch src/module/players/external src/formats/test regression/Makefile 'regression/atom_ant.ay*'
