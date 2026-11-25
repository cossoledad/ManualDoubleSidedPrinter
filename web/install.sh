#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
  printf '%s\n' 'Run sudo ./install.sh from the extracted release directory.' >&2
  exit 1
fi
if [ "$(uname -m)" != aarch64 ]; then
  printf '%s\n' 'This release requires Ubuntu ARM64 (aarch64).' >&2
  exit 1
fi

source_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
target=/opt/mdsprinter-web
unit=mdsprinter-web.service
unit_file=/etc/systemd/system/$unit
config=/etc/mdsprinter-web.env
if [ "$source_dir" = "$target" ]; then
  printf '%s\n' 'Extract the release to a temporary directory before running the installer.' >&2
  exit 1
fi
"$source_dir/bin/node" --check "$source_dir/server.mjs"

if ! command -v lp >/dev/null || ! command -v lpstat >/dev/null || ! command -v qpdf >/dev/null ||
   ! command -v pdftoppm >/dev/null || ! command -v pdfinfo >/dev/null; then
  apt-get update
  apt-get install -y cups-client qpdf poppler-utils
fi

install -d -m 755 /opt
work=$(mktemp -d /opt/.mdsprinter-web.XXXXXX)
stage=$work/new
stopped=false
swapped=false
restore=false
cleanup() {
  result=$?
  if [ "$result" -ne 0 ] && [ "$restore" = true ]; then
    systemctl stop "$unit" || :
    if [ "$swapped" = true ]; then mv -T -- "$target" "$work/failed"; fi
    if [ -d "$work/previous" ]; then mv -T -- "$work/previous" "$target"; fi
    if [ -f "$work/previous.service" ]; then
      install -m 644 "$work/previous.service" "$unit_file"
    else
      systemctl disable "$unit" || :
      rm -f -- "$unit_file"
    fi
    systemctl daemon-reload || :
    if [ "$stopped" = true ]; then systemctl start "$unit" || :; fi
  fi
  rm -rf -- "$work"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

install -d -m 755 "$stage" "$stage/bin" "$stage/public"
install -m 755 "$source_dir/bin/node" "$stage/bin/node"
install -m 755 "$source_dir/start.sh" "$source_dir/install.sh" "$stage/"
install -m 644 "$source_dir/"*.mjs "$source_dir/README.md" "$source_dir/NODE-LICENSE" "$source_dir/$unit" "$stage/"
install -m 644 "$source_dir/public/"* "$stage/public/"
if [ ! -e "$config" ]; then
  umask 022
  (set -C; printf '%s\n' '# Optional. CUPS queues are discovered automatically.' 'MDS_PORT=30128' > "$config")
fi
if [ -f "$unit_file" ]; then cp -- "$unit_file" "$work/previous.service"; fi
if [ -e "$target/data" ]; then
  printf '%s\n' 'Found a portable-install data directory in /opt/mdsprinter-web/data. Keep it outside /opt/mdsprinter-web before upgrading.' >&2
  exit 1
fi
if systemctl is-active --quiet "$unit"; then
  systemctl stop "$unit"
  stopped=true
fi
restore=true
if [ -e "$target" ] || [ -L "$target" ]; then mv -T -- "$target" "$work/previous"; fi
mv -T -- "$stage" "$target"
swapped=true
install -m 644 "$target/$unit" "$unit_file"
systemctl daemon-reload
systemctl enable --now "$unit"
pid=$(systemctl show "$unit" --property=MainPID --value)
sleep 1
systemctl is-active --quiet "$unit"
test "$pid" -gt 0 && test "$pid" = "$(systemctl show "$unit" --property=MainPID --value)" || exit 1
restore=false

printf '%s\n' 'MDSPrinter is running. Default URL: http://<Ubuntu LAN IP>:30128'
