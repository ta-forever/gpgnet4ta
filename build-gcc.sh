SOURCE_DIR=$(dirname $(realpath $0))

cmake \
    -ENABLE_IRC=OFF \
    -DCMAKE_INSTALL_PREFIX=$(pwd) \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS=-s \
    $SOURCE_DIR || exit
make && make install || exit

cp $SOURCE_DIR/taforever.ini.template bin/
