#!/bin/bash

rm -rf obj
mkdir obj

if [ -z "$BUILDENV" ]; then
        BUILDENV=/usr
fi

GPPCMD="g++"
STRIPCMD="strip"

if [ "$ARCH" == "x86" ];
then
	ARCHFLAG="-m32"
	ARCHNAME="i686"
elif [ "$ARCH" == "armhf" ];
then
        ARCHFLAG=""
        ARCHNAME="armhf"
        GPPCMD="arm-linux-gnueabihf-g++"
        GCCCMD="arm-linux-gnueabihf-gcc"
        STRIPCMD="arm-linux-gnueabihf-strip"
else
	ARCHFLAG="-m64"
	ARCHNAME="x86_64"
fi

if [ -e /Applications ];
then
	STRIPCMD="x86_64-apple-darwin12-strip"
	GPPCMD="o64-clang++"
	GPP="$GPPCMD -D`arch` -D_UNIX -I$BUILDENV/include -I$BUILDENV/include/mariadb -I. -c  -O2 -ggdb -o "
else
	GPP="$GPPCMD $ARCHFLAG -D$ARCHNAME -D_UNIX -I$BUILDENV/include -I$BUILDENV/include/mariadb -I. -c  -O2 -ggdb -o "
fi

echo " "
echo "Compiling resources..."
php rescc.php || exit 1
$GPPCMD $ARCHFLAG -o obj/resdata.obj -c resdata.cpp || exit 1

echo " "
echo "Compiling..."

echo "main.cpp..."
$GPP obj/main.obj main.cpp || exit 1
echo "SetupServer.cpp..."
$GPP obj/SetupServer.obj SetupServer.cpp || exit 1
echo "SetupServer_install.cpp..."
$GPP obj/SetupServer_install.obj SetupServer_install.cpp || exit 1
echo "HTTPServer.cpp..."
$GPP obj/HTTPServer.obj HTTPServer.cpp || exit 1
echo "UI.cpp..."
$GPP obj/UI.obj UI.cpp || exit 1
echo "Template.cpp..."
$GPP obj/Template.obj Template.cpp || exit 1
echo "Language.cpp..."
$GPP obj/Language.obj Language.cpp || exit 1
echo "Utils.cpp..."
$GPP obj/Utils.obj Utils.cpp || exit 1
echo "mysql_db.cpp..."
$GPP obj/mysql_db.obj mysql_db.cpp || exit 1
echo "mysql_result.cpp..."
$GPP obj/mysql_result.obj mysql_result.cpp || exit 1
echo "md5.c..."
$GPP obj/md5.obj md5.c || exit 1

echo " "
echo "Linking..."

$GPPCMD $ARCHFLAG -ggdb -o setup -L$BUILDENV/lib -L$BUILDENV/lib/mariadb obj/*.obj -lmariadb -lssl -lcrypto -ldialog -lncurses -lncursesw -Wl,-rpath -Wl,./libs/ || exit 1

if [ -e /Applications ];
then
	x86_64-apple-darwin12-install_name_tool -change "/home/patrick/sandbox/mariadb-native-client/build/libmariadb/libmariadb.2.dylib" "@loader_path/libs/libmariadb.2.dylib" setup
fi

cp setup ../debug/

echo "Stripping symbols..."
$STRIPCMD setup || exit 1

echo " "
echo "Done"
echo " "
