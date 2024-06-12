#!/bin/sh

set -ex

# use LLVM tools linked to symbiotic's install directory
ENV_CMD="$(../install/bin/symbiotic --debug=all --dump-env-cmd)"
eval "$ENV_CMD"
PATH="$PWD/../install/bin/:$PATH"

# sanitizer settings
export ASAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=print_stacktrace=1

# clear the environment
# (We don't want to have ASAN & friends baked into the verified bitcode.)
unset CFLAGS
unset CXXFLAGS
unset CPPFLAGS
unset LDFLAGS

# GitHub Actions define this variable
if [ -n "$CI" ]; then
  ci_args='--color --with-integrity-check'
fi

./test_runner.py "$@" $ci_args ./reach.set
./test_runner.py "$@" $ci_args --32 ./reach.set
