#!/bin/sh
set -x
set -e
yarn run node-gyp clean
IPHONEOS_DEPLOYMENT_TARGET=8.0 yarn run node-gyp configure rebuild --arch=arm64 --debug --release --verbose
make -C build BUILDTYPE=Debug V=1 -j4
make -C build BUILDTYPE=Release V=1 -j4
