#!/usr/bin/env bash
# Discover Chocolatey / Shining Light OpenSSL on Windows and export CMake hints.
# Used by the Studio package job: choco layout and install path vary by version.
set -euo pipefail

to_cmake_path() {
  local raw="$1"
  if command -v cygpath >/dev/null 2>&1; then
    cygpath -m "$raw"
  else
    printf '%s\n' "${raw//\\//}"
  fi
}

candidates=()
if [[ -n "${OPENSSL_ROOT_DIR:-}" ]]; then
  candidates+=("${OPENSSL_ROOT_DIR}")
fi
candidates+=(
  "C:/Program Files/OpenSSL-Win64"
  "C:/Program Files/OpenSSL"
  "C:/Program Files (x86)/OpenSSL-Win64"
  "C:/Program Files (x86)/OpenSSL"
)
for d in /c/Program\ Files/OpenSSL* /c/Program\ Files\ \(x86\)/OpenSSL*; do
  if [[ -d "$d" ]]; then
    candidates+=("$(to_cmake_path "$d")")
  fi
done

find_header_dir() {
  local root="$1"
  if [[ -f "${root}/include/openssl/ssl.h" ]]; then
    printf '%s\n' "${root}/include"
    return 0
  fi
  return 1
}

find_msvc_lib() {
  local root="$1" name="$2"
  local candidate
  for candidate in \
    "${root}/lib/VC/x64/MD/${name}.lib" \
    "${root}/lib/VC/x64/MT/${name}.lib" \
    "${root}/lib/${name}.lib" \
    "${root}/lib64/${name}.lib"
  do
    if [[ -f "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  done
  return 1
}

root=""
include=""
crypto=""
ssl=""
for candidate in "${candidates[@]}"; do
  [[ -d "$candidate" ]] || continue
  if ! include="$(find_header_dir "$candidate")"; then
    continue
  fi
  if crypto="$(find_msvc_lib "$candidate" libcrypto)" \
    && ssl="$(find_msvc_lib "$candidate" libssl)"; then
    root="$candidate"
    break
  fi
done

if [[ -z "$root" ]]; then
  echo "error: Windows OpenSSL not found (need include/openssl/ssl.h and MSVC libcrypto/libssl)." >&2
  echo "searched:" >&2
  printf '  %s\n' "${candidates[@]}" >&2
  echo "Program Files entries matching *ssl*:" >&2
  ls -1 "/c/Program Files" 2>/dev/null | grep -i ssl || true
  ls -1 "/c/Program Files (x86)" 2>/dev/null | grep -i ssl || true
  exit 1
fi

root="$(to_cmake_path "$root")"
include="$(to_cmake_path "$include")"
crypto="$(to_cmake_path "$crypto")"
ssl="$(to_cmake_path "$ssl")"

echo "Discovered OpenSSL at ${root}"
echo "  OPENSSL_INCLUDE_DIR=${include}"
echo "  OPENSSL_CRYPTO_LIBRARY=${crypto}"
echo "  OPENSSL_SSL_LIBRARY=${ssl}"

if [[ -n "${GITHUB_ENV:-}" ]]; then
  {
    echo "OPENSSL_ROOT_DIR=${root}"
    echo "OPENSSL_INCLUDE_DIR=${include}"
    echo "OPENSSL_CRYPTO_LIBRARY=${crypto}"
    echo "OPENSSL_SSL_LIBRARY=${ssl}"
    echo "CMAKE_PREFIX_PATH=${root}"
  } >> "${GITHUB_ENV}"
fi
