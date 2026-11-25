#!/bin/sh
set -eu

base=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
export MDS_DATA=${MDS_DATA:-"$base/data"}
export MDS_BIND=${MDS_BIND:-0.0.0.0}
export MDS_PORT=${MDS_PORT:-30128}
exec "$base/bin/node" "$base/server.mjs"
