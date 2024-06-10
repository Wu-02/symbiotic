#!/bin/sh

if [  "x$UPDATE" = "x1" -o -z "$(ls -A $SRCDIR/klee)" ]; then
	git_submodule_init
fi

pushd klee
BASE="" BITWUZLA_VERSION="0.3.2" BITWUZLA_COMMIT="0.3.2" SOLVERS="bitwuzla" LLVM_VERSION=14 ENABLE_OPTIMIZED=1 ENABLE_DEBUG=0 DISABLE_ASSERTIONS=1 REQUIRES_RTTI=1 GTEST_VERSION=1.11.0 SQLITE_VERSION=3400100 UCLIBC_VERSION=klee_uclibc_v1.3 COVERAGE=0 KLEE_RUNTIME_BUILD=${BUILD_TYPE} USE_LIBCXX=0 USE_TCMALLOC=0 ENABLE_DOXYGEN=1 ./scripts/build/build.sh klee

# Our version of KLEE does not work with STP now
# STP_FLAGS=
# if [ "$BUILD_STP" = "yes" -o -d $ABS_SRCDIR/stp ]; then
# 	STP_FLAGS="-DENABLE_SOLVER_STP=ON -DSTP_DIR=${ABS_SRCDIR}/stp"
# fi
STP_FLAGS="-DENABLE_SOLVER_STP=OFF"
Z3_FLAGS="-DENABLE_SOLVER_Z3=OFF"
BITWUZLA_FLAGS="-DENABLE_SOLVER_BITWUZLA=OFF"

if [ "$BUILD_Z3" = "yes" ]; then
	Z3_FLAGS="-DENABLE_SOLVER_Z3=ON"
	if [ -d ${ABS_SRCDIR}/z3 ]; then
		Z3_FLAGS="$Z3_FLAGS -DCMAKE_LIBRARY_PATH=${ABS_SRCDIR}/z3/build/"
		Z3_FLAGS="$Z3_FLAGS -DCMAKE_INCLUDE_PATH=${ABS_SRCDIR}/z3/src/api"
	fi
fi
if [ "$BUILD_BITWUZLA" = "yes" ]; then
	BITWUZLA_FLAGS="-DENABLE_SOLVER_BITWUZLA=ON"
fi
if [ "$HAVE_Z3" = "no" -a "$BUILD_Z3" = "no" -a "$BUILD_BITWUZLA" = "no" ]; then
	exitmsg "KLEE needs Z3 or Bitwuzla library"
fi

#if [ ! -d CMakeFiles ]; then
	# use our zlib, if we compiled it
	ZLIB_FLAGS=
	if [ -d $ABS_RUNDIR/zlib ]; then
		ZLIB_FLAGS="-DZLIB_LIBRARY=-L${PREFIX}/lib;-lz"
		ZLIB_FLAGS="$ZLIB_FLAGS -DZLIB_INCLUDE_DIR=$PREFIX/include"
	fi

	if which lit &>/dev/null; then
		HAVE_LIT=on
	else
		HAVE_LIT=off
	fi

	EXTRA_FLAGS=""
	if [ -d $ABS_SRCDIR/googletest ]; then
		HAVE_GTEST="yes"
		EXTRA_FLAGS="-DGTEST_SRC_DIR=$ABS_SRCDIR/googletest"
	fi

	if [ "$HAVE_LIT"="yes" -a "$HAVE_GTEST" = "yes" ]; then
		ENABLE_TESTS="on"
	else
		ENABLE_TESTS="off"
	fi

	cmake .. -DCMAKE_INSTALL_PREFIX=$LLVM_PREFIX \
		-DCMAKE_BUILD_TYPE=${BUILD_TYPE}\
		-DKLEE_RUNTIME_BUILD_TYPE=${BUILD_TYPE} \
		-DLLVM_CONFIG_BINARY=$(abspath ${LLVM_CONFIG}) \
		-DENABLE_UNIT_TESTS=${ENABLE_TESTS} \
		-DENABLE_SYSTEM_TESTS=${ENABLE_TESTS} \
		-DENABLE_TCMALLOC=${ENABLE_TCMALLOC} \
		-DENABLE_FLOATING_POINT=ON \
		-DENABLE_FP_RUNTIME=ON \
		$ZLIB_FLAGS $Z3_FLAGS $STP_FLAGS $BITWUZLA_FLAGS ${EXTRA_FLAGS}\
		|| clean_and_exit 1 "git"
#fi

if [ "$UPDATE" = "1" ]; then
	git fetch --all
	git checkout $KLEE_BRANCH
	git pull
fi

# clean runtime libs, it may be 32-bit from last build
# make -C runtime -f Makefile.cmake.bitcode clean 2>/dev/null

# build 64-bit libs and install them to prefix
(build && make install) || exit 1

mkdir -p $LLVM_PREFIX/lib32/klee/runtime
mv $LLVM_PREFIX/lib/klee/runtime/*32_*.bca $LLVM_PREFIX/lib32/klee/runtime \
	|| exitmsg "Cannot move 32-bit klee runtime lib files."

popd
