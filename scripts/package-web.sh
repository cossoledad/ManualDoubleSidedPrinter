#!/bin/sh
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd -P)
node_version=24.22.3
node_sha=6e51a2ada00f0011cc266f65d98922b403d7ad88f809ca2aa615a5fe1ce6e86c
node_release="node-v$node_version-linux-arm64"
version=$(sed -n 's/^project(ManualDoubleSidedPrinter VERSION \([^ ]*\) .*/\1/p' "$root/CMakeLists.txt")
test -n "$version"
output="$root/artifacts/web"
name=mdsprinter-web
temp=$(mktemp -d)
trap 'rm -rf -- "$temp"' EXIT

curl --fail --silent --show-error --location --retry 3 \
  "https://nodejs.org/dist/v$node_version/$node_release.tar.xz" -o "$temp/node.tar.xz"
printf '%s  %s\n' "$node_sha" "$temp/node.tar.xz" | sha256sum -c -
tar -xJf "$temp/node.tar.xz" -C "$temp" "$node_release/bin/node" "$node_release/LICENSE"

stage="$temp/$name"
mkdir -p "$stage/bin" "$output"
install -m 755 "$temp/$node_release/bin/node" "$stage/bin/node"
install -m 755 "$root/web/start.sh" "$root/web/install.sh" "$stage/"
cp "$temp/$node_release/LICENSE" "$stage/NODE-LICENSE"
cp "$root/web/duplex.mjs" "$root/web/jobs.mjs" "$root/web/printing.mjs" "$root/web/server.mjs" \
  "$root/web/mdsprinter-web.service" "$root/web/README.md" "$stage/"
cp -R "$root/web/public" "$stage/public"
if [ "$(uname -m)" = aarch64 ]; then
  "$stage/bin/node" --version
  "$stage/bin/node" --check "$stage/server.mjs"
fi
artifact="MDSPrinter-Web-linux-arm64-v$version.tar.xz"
tar -cJf "$output/$artifact" -C "$temp" "$name"
(cd "$output" && sha256sum "$artifact" > "$artifact.sha256")
printf '%s\n' "$output/$artifact"
