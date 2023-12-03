conan install . -s=build_type=Release -s=arch=x86 -o=qt/*:shared=True --build=missing
pause
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build\generators\conan_toolchain.cmake -A x86
pause