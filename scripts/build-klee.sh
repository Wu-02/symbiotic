#!/bin/sh

if [  "x$UPDATE" = "x1" -o -z "$(ls -A $SRCDIR/klee)" ]; then
	git_submodule_init
fi

deps="solvers"
# we should reuse gtest if we built llvm
if [ ! -z "$WITH_LLVM" -a $LLVM_MAJOR_VERSION -ge 9 ]; then
	deps="$deps gtest"
fi

mkdir -p klee/build-${LLVM_VERSION}-${BUILD_TYPE}
pushd klee/build-${LLVM_VERSION}-${BUILD_TYPE}
BASE=$(abspath ./subprojects) BITWUZLA_VERSION="0.3.2" BITWUZLA_COMMIT="0.3.2" SOLVERS="bitwuzla" GTEST_VERSION=1.11.0 ../scripts/build/build.sh $deps
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
	src=`compgen -G subprojects/bitwuzla-*-install/`
	BITWUZLA_FLAGS="$BITWUZLA_FLAGS -DCMAKE_PREFIX_PATH=$(abspath $src)"
fi
if [ "$HAVE_Z3" = "no" -a "$BUILD_Z3" = "no" -a "$BUILD_BITWUZLA" = "no" ]; then
	exitmsg "KLEE needs Z3 or Bitwuzla library"
fi

#if [ ! -d CMakeFiles ]; then
	if which lit &>/dev/null; then
		HAVE_LIT=yes
	else
		HAVE_LIT=no
	fi

	EXTRA_FLAGS=""
	if src=`compgen -G subprojects/googletest*/`; then
		HAVE_GTEST="yes"
		EXTRA_FLAGS="$EXTRA_FLAGS -DGTEST_SRC_DIR=$(abspath $src)"
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
		-DENABLE_TCMALLOC=OFF \
		-DENABLE_FLOATING_POINT=ON \
		-DENABLE_FP_RUNTIME=ON \
		-DENABLE_ZLIB=ON \
		-DENABLE_KLEE_ASSERTS=OFF \
		$Z3_FLAGS $BITWUZLA_FLAGS ${EXTRA_FLAGS}\
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
