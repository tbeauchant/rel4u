#!/bin/sh
set -eu
ulimit -c 0
ulimit -s 512
exec "$@"
