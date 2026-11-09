#!/usr/bin/env bash
# Clone vcpkg at $VCPKG_COMMIT into $MPT_VCPKG_DIR and bootstrap it, retrying on network errors.
# The bootstrap downloads the vcpkg tool from GitHub releases, which returns 504s when GitHub is
# degraded (run 42568270791). Each attempt starts from an empty directory, so a partial clone or
# a half-written tool from a failed attempt is never reused.
set -euo pipefail

: "${MPT_VCPKG_DIR:?}" "${VCPKG_COMMIT:?}" "${VCPKG_DEFAULT_BINARY_CACHE:?}"
attempts=3

mkdir -p "$VCPKG_DEFAULT_BINARY_CACHE"
for ((i = 1; ; i++)); do
  rm -rf "$MPT_VCPKG_DIR"
  if git clone https://github.com/microsoft/vcpkg "$MPT_VCPKG_DIR" \
     && git -C "$MPT_VCPKG_DIR" checkout "$VCPKG_COMMIT" \
     && if [ "${RUNNER_OS:-}" = "Windows" ]; then "$MPT_VCPKG_DIR/bootstrap-vcpkg.bat" -disableMetrics
        else "$MPT_VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics; fi; then
    break
  fi
  if ((i >= attempts)); then
    echo "::error::vcpkg bootstrap failed after $i attempts"
    exit 1
  fi
  delay=$((30 * i))
  echo "::warning::vcpkg bootstrap attempt $i of $attempts failed; retrying in ${delay} s"
  sleep "$delay"
done

# The steps after this one use this vcpkg, not the one msvc-dev-cmd points VCPKG_ROOT at.
echo "VCPKG_ROOT=$MPT_VCPKG_DIR" >> "$GITHUB_ENV"
