set -x
set -e
node-gyp rebuild --verbose | tee BuildOutput.txt
cd build
rm -f compile.sh
echo 'set -x' >> compile.sh && echo 'set -e' >> compile.sh && cat ../BuildOutput.txt | egrep -v '^[ \t]+rm' | sed 's/x86_64/arm64/g' | sed 's/-mmacosx-version-min=[0-9.]*/-miphoneos-version-min=8.0/' | sed 's/[.]node-gyp\/[^\/]*/nodeios/g' | sed 's/-miphoneos-version-min=/-isysroot \/Applications\/Xcode-10.1.app\/Contents\/Developer\/Platforms\/iPhoneOS.platform\/Developer\/SDKs\/iPhoneOS12.1.sdk -miphoneos-version-min=/' | sed 's/^  \([^ ]*\)/  \1 -DTARGET_OS_IPHONE=1 -D__IPHONEOS__/g' | sed 's/-framework OpenGL /-framework OpenGLES /g' | sed 's/-framework AudioUnit //g' | sed 's/-framework Cocoa //g' >> compile.sh && echo find Release/obj.target -name '*.o' \| xargs ar crs Release/libexokit.a >> compile.sh && echo find Debug/obj.target -name '*.o' \| xargs ar crs Debug/libexokit.a >> compile.sh
sh compile.sh
