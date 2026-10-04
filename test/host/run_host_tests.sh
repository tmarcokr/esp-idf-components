#!/usr/bin/env bash
# Builds and runs every test/host/*.cpp with the host compiler. Exits 1 if any build or run fails.
# CXX selects the compiler (default g++); HOST_TEST_CXXFLAGS replaces the optimization and sanitizer flags.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

cxx="${CXX:-g++}"
base_flags=(-std=c++20 -Wall -Wextra -Werror)
if [[ -n "${HOST_TEST_CXXFLAGS:-}" ]]; then
    read -r -a extra_flags <<< "$HOST_TEST_CXXFLAGS"
else
    extra_flags=(-O1 -g "-fsanitize=address,undefined" -fno-sanitize-recover=all)
fi

shopt -s nullglob
tests=(test/host/*.cpp)
if (( ${#tests[@]} == 0 )); then
    echo "No host tests found in test/host/." >&2
    exit 1
fi

include_flags=()
while IFS= read -r include_dir; do
    include_flags+=(-I "$include_dir")
done < <(find components -type d -name include -not -path '*/managed_components/*' | sort)

build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

passed=()
failed=()
for source in "${tests[@]}"; do
    name="$(basename "$source" .cpp)"
    echo "::group::$name"
    if "$cxx" "${base_flags[@]}" "${extra_flags[@]}" "${include_flags[@]}" "$source" -o "$build_dir/$name" &&
        "$build_dir/$name"; then
        passed+=("$name")
        result="PASS"
    else
        failed+=("$name")
        result="FAIL"
    fi
    echo "::endgroup::"
    echo "$name: $result"
done

echo "Host tests: ${#passed[@]} passed, ${#failed[@]} failed (compiler: $("$cxx" --version | head -n 1))"
if (( ${#failed[@]} != 0 )); then
    echo "Failed: ${failed[*]}" >&2
    exit 1
fi
