#!/bin/bash
# Build the native ASan replica of the 3DS core smoke test.
set -e
cd "$(dirname "$0")/../.."

SOURCES=$(sed -n 's/^    \${VC_SRC}\/\(.*\.cpp\)$/src\/\1/p' 3ds/CMakeLists.txt)

clang++ -std=c++17 -g -O1 -fsanitize=address,undefined \
    -DVC_BUILD_NAME='"native-test"' -DVC_NO_GL -DGLM_ENABLE_EXPERIMENTAL \
    $EXTRA_DEFS \
    -Isrc -I3ds/src -I3ds/external/glm -I3ds/external/entt/src \
    -I/opt/homebrew/include \
    $SOURCES \
    3ds/src/stubs/scripting_stub.cpp \
    3ds/src/stubs/input_stub.cpp \
    3ds/src/gen/CoreGenerator.cpp \
    3ds/test_native/native_main.cpp \
    3ds/test_native/render_stubs.cpp \
    -L/opt/homebrew/lib -lpng -lz \
    -o /tmp/vc_native_test
echo "built /tmp/vc_native_test"
