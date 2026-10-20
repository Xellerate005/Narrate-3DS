#!/usr/bin/env bash
# Fetches the FAAD2 AAC decoder sources into deps/faad2 (run once, from the project folder).
set -e
cd "$(dirname "$0")"
mkdir -p deps
if [ -f deps/faad2/libfaad/decoder.c ]; then
  echo "FAAD2 already present in deps/faad2"
  exit 0
fi
if command -v git >/dev/null 2>&1; then
  git clone --depth 1 --branch 2.10.1 https://github.com/knik0/faad2.git deps/faad2 \
    || git clone --depth 1 https://github.com/knik0/faad2.git deps/faad2
elif command -v curl >/dev/null 2>&1; then
  curl -L https://github.com/knik0/faad2/archive/refs/heads/master.tar.gz | tar xz -C deps
  mv deps/faad2-master deps/faad2
else
  echo "Need git or curl. Alternatively download https://github.com/knik0/faad2 and unpack it so that"
  echo "deps/faad2/libfaad/decoder.c exists."
  exit 1
fi
echo "FAAD2 ready."
