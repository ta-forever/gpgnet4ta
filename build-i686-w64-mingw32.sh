SOURCE_DIR=$(dirname $(realpath $0))

MXE_PATH=/usr/lib/mxe
MXE_PREFIX=i686-w64-mingw32.shared
export PATH=$MXE_PATH/usr/bin:$PATH

apt-get install $MXE_PREFIX-cryptopp

$MXE_PREFIX-cmake \
    -ENABLE_IRC=OFF \
    -DCMAKE_INSTALL_PREFIX=$(pwd) \
    -DCMAKE_CXX_FLAGS=-s \
    -DUID_PUBKEY_MODULUS="$(openssl rsa -noout -inform PEM -in ${SOURCE_DIR}/faf_pub.pem -pubin -modulus)" \
    $SOURCE_DIR || exit
make && make install || exit

for exe in bin/*.exe; do
    $MXE_PATH/tools/copydlldeps.sh \
        --infile ${exe} \
        --destdir bin \
        --recursivesrcdir $MXE_PATH/usr/$MXE_PREFIX/ \
        --srcdir $SOURCE_DIR/ \
        --copy \
        --enforcedir $MXE_PATH/usr/$MXE_PREFIX/qt5/plugins/platforms/ \
        --objdump $MXE_PATH/usr/bin/$MXE_PREFIX-objdump || exit
done

cp $SOURCE_DIR/taforever.ini.template bin/
cp $SOURCE_DIR/online.dll bin/
