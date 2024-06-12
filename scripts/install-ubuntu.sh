#!/bin/sh

set -e

apt-get update

PACKAGES="llvm-14 lld curl wget rsync make cmake unzip gcc-multilib xz-utils python3 pipx zlib1g-dev git build-essential ninja-build pkg-config m4 zlib1g-dev libsqlite3-dev libboost-all-dev libgmp-dev libpolly-14-dev"

# install clang if there is not suitable compiler
if ! g++ --version &>/dev/null; then
	if ! clang++ --version &>/dev/null; then
		PACKAGES="$PACKAGES clang"
	fi
fi

INSTALL_Z3="N"
INSTALL_LLVM="N"
INSTALL_SQLITE="N"
INSTALL_ZLIB="N"

# Ask for these as the user may have his/her own build
if !  dpkg -l | grep -q libz3-dev; then
	echo "Z3 not found, should I install it? [y/N]"
	read INSTALL_Z3
fi

if !  dpkg -l | grep -q 'llvm.*-dev'; then
	echo "LLVM not found, should I install it? [y/N]"
	read INSTALL_LLVM
fi

if ! dpkg -l | grep -q 'libsqlite3-dev'; then
	echo "SQLite not found, should I install it? [y/N]"
	read INSTALL_SQLITE
fi
if ! dpkg -l | grep -q 'zlib'; then
	echo "zlib not found, should I install it? [y/N]"
	read INSTALL_ZLIB
fi

if [ "$INSTALL_Z3" = "y" ]; then
	PACKAGES="$PACKAGES libz3-dev"
fi
if [ "$INSTALL_LLVM" = "y" ]; then
	PACKAGES="$PACKAGES llvm"
fi
if [ "$INSTALL_SQLITE" = "y" ]; then
	PACKAGES="$PACKAGES libsqlite3-dev"
fi
if [ "$INSTALL_ZLIB" = "y" ]; then
	PACKAGES="$PACKAGES zlib1g"
fi

apt-get install $PACKAGES
pipx install meson lit