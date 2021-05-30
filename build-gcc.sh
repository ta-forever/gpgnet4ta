SOURCE_DIR=$(dirname $(realpath $0))

mkdir -p libcommuni && pushd libcommuni && \
    qmake $SOURCE_DIR/libcommuni &&
    make || exit
popd

cmake \
    -DLIBCOMMUNI_DIR=$(pwd)/libcommuni \
    -DCMAKE_INSTALL_PREFIX=$(pwd) \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS=-s \
    $SOURCE_DIR || exit
make && make install || exit

cp libcommuni/lib/* bin/
cp $SOURCE_DIR/taforever.ini.template bin/
