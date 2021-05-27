SOURCE_DIR=$(dirname $(realpath $0))

MXE_PATH=/usr/lib/mxe
export PATH=$MXE_PATH/usr/bin:$PATH

mkdir -p libcommuni && pushd libcommuni && \
    $MXE_PATH/usr/i686-w64-mingw32.shared/qt5/bin/qmake $SOURCE_DIR/libcommuni &&
    make || exit
popd

i686-w64-mingw32.shared-cmake \
    -DLIBCOMMUNI_DIR=$(pwd)/libcommuni \
    -DCMAKE_INSTALL_PREFIX=$(pwd) \
    -DCMAKE_CXX_FLAGS=-s \
    $SOURCE_DIR || exit
make && make install || exit

for exe in bin/*.exe; do
    $MXE_PATH/tools/copydlldeps.sh \
        --infile ${exe} \
        --destdir bin \
        --recursivesrcdir $MXE_PATH/usr/i686-w64-mingw32.shared/ \
        --srcdir $SOURCE_DIR/ \
        --copy \
        --enforcedir $MXE_PATH/usr/i686-w64-mingw32.shared/qt5/plugins/platforms/ \
        --objdump $MXE_PATH/usr/bin/i686-w64-mingw32.shared-objdump || exit
done

cp libcommuni/bin/* bin/
