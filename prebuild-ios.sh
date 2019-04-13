set -x
set -e
node-gyp rebuild --verbose | sponge BuildOutput.txt
