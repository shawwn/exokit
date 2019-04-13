#!/bin/sh
set -x
set -e
yarn run node-gyp clean
MACOS_DEPLOYMENT_TARGET=10.1 yarn run node-gyp configure rebuild --debug --release --verbose
make -C build BUILDTYPE=Debug V=1 -j4
make -C build BUILDTYPE=Release V=1 -j4
