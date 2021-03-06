#!/bin/sh

set -e

usage() { echo "usage: `basename $0` version" ; }

[ $# -eq 1 ] || { usage >&2 ; exit 1 ; }

VERSION="$1"

dch -v $VERSION+dfsg.1-1
dch -r
uscan --repack --compression xz --force
gpg --sign --armor --detach-sign ../notcurses_$VERSION+dfsg.1.orig.tar.xz
# FIXME this seems to upload to $VERSION.dfsg as opposed to $VERSION+dfsg?
github-asset dankamongmen/notcurses upload v$VERSION ../notcurses_$VERSION+dfsg.1.orig.tar.xz ../notcurses_$VERSION+dfsg.1.orig.tar.xz.asc
git commit -m "v$VERSION" -a
gbp import-orig ../notcurses_$VERSION+dfsg.1.orig.tar.xz
git push --tags
dpkg-buildpackage --build=source
cd .. && export TERM=xterm-256color && sudo pbuilder build *dsc
