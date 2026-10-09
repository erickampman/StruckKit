(Linux notes. On macOS, see MACOS.md.)

CONFIGURE

cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

BUILD (also symlinks build/bin/StruckKit.clap into ~/.clap via a post-build step)

cmake --build build -j

CLEAN 

cmake --build build -j --clean-first


macOS (universal: Apple Silicon + Intel; drop the option for a native-only build)

cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DSTRUCKKIT_MACOS_UNIVERSAL=ON
cmake --build build -j
(symlinks into ~/Library/Audio/Plug-Ins/CLAP; check with: lipo -archs build/bin/StruckKit.clap/Contents/MacOS/StruckKit)

Config Dump build:

    cd ~/dev/dev-clap/StruckKit
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DSTRUCKKIT_PRESET_DUMP=ON
cmake --build build -j"$(nproc)"
