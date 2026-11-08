#!/bin/sh
# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: 2026 SEN Labs e.U.
#
# Builds the packages basho (the server and the command location) and basho_devel (the headers and liblocation.a) from what ./build.sh made, on Haiku. The version is the one of
# the resource of the server (src/server/location_server.rdef: 0.1.0, development 1 is 0.1.0~develop.1); REVISION (default 1) is the revision of the package. The packages are
# written to the folder of the script.
set -e
cd "$(dirname "$0")"
[ -x bin/location_server ] && [ -x bin/location ] && [ -f lib/liblocation.a ] || { echo "run ./build.sh first" >&2; exit 1; }

ARCH=$(getarch)
REVISION=${REVISION:-1}
RDEF=src/server/location_server.rdef
field() { sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*\\([0-9]*\\).*/\\1/p" $RDEF | head -n1; }
SIGNATURE=$(sed -n 's/^resource app_signature "\(.*\)";/\1/p' $RDEF)
variety=$(sed -n 's/^[[:space:]]*variety[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $RDEF | head -n1)
internal=$(sed -n 's/^[[:space:]]*internal[[:space:]]*=[[:space:]]*\([0-9]*\).*/\1/p' $RDEF | head -n1)
VERSION="$(field major).$(field middle).$(field minor)"
# the stage and its count are in the resource (the About text shows 0.1.0-develop.1 for the package 0.1.0~develop.1); 0 development, 2 beta
case "$variety" in
	0) suffix=develop; [ "${internal:-0}" -gt 0 ] && suffix=develop.$internal; suffix=${BASHO_VERSION_SUFFIX:-$suffix} ;;
	2) suffix=beta${internal:-1} ;;
	*) suffix= ;;
esac
[ -n "$suffix" ] && VERSION="$VERSION~$suffix"
VERSION="$VERSION-$REVISION"

# the program must carry the signature that the roster looks for
attr=$(catattr -d BEOS:APP_SIG bin/location_server 2>/dev/null || true)
[ "$attr" = "$SIGNATURE" ] || { echo "location_server has the signature '$attr', not '$SIGNATURE': run ./build.sh" >&2; exit 1; }

STAGE=package-build
rm -rf $STAGE
mkdir -p $STAGE/basho/bin $STAGE/basho/servers $STAGE/basho/documentation/packages/basho
cp -a bin/location_server $STAGE/basho/servers/
cp -a bin/location $STAGE/basho/bin/
cp -a README.md LICENSE docs/design-notes.md $STAGE/basho/documentation/packages/basho/
sed -e "s|@VERSION@|$VERSION|g" -e "s|@ARCH@|$ARCH|g" package/PackageInfo.in > $STAGE/basho/.PackageInfo

mkdir -p $STAGE/devel/develop/headers $STAGE/devel/develop/lib $STAGE/devel/documentation/packages/basho_devel
cp -R include/location $STAGE/devel/develop/headers/
cp -a lib/liblocation.a $STAGE/devel/develop/lib/
cp -a LICENSE $STAGE/devel/documentation/packages/basho_devel/
sed -e "s|@VERSION@|$VERSION|g" -e "s|@ARCH@|$ARCH|g" package/PackageInfo.devel.in > $STAGE/devel/.PackageInfo

rm -f basho-*.hpkg basho_devel-*.hpkg
package create -C $STAGE/basho "basho-$VERSION-$ARCH.hpkg"
package create -C $STAGE/devel "basho_devel-$VERSION-$ARCH.hpkg"
rm -rf $STAGE
ls -l basho-*.hpkg basho_devel-*.hpkg
