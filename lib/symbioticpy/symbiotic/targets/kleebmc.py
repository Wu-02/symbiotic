"""
BenchExec is a framework for reliable benchmarking.
This file is part of BenchExec.

Copyright (C) 2007-2015  Dirk Beyer
Copyright (C) 2016-2019  Marek Chalupa
All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
"""

from symbiotic.utils.utils import process_grep
from symbiotic.utils import dbg
from symbiotic.exceptions import SymbioticException


from . tool import SymbioticBaseTool
from .. utils.process import runcmd

try:
    from benchexec.tools.template import BaseTool
except ImportError:
    # fall-back solution (at least for now)
    from symbiotic.benchexec.tools.template import BaseTool

from . klee import SymbioticTool as KleeTool

class SymbioticTool(BaseTool, SymbioticBaseTool):
    """
    Symbiotic tool info object
    """

    def __init__(self, opts):
        self.klee = KleeTool(opts)
        self._options = opts
        self._env = None
        self._hit_threads = False
        self._options.phase = 1;

    def verifiers(self):
        prp = self._options.property
        if prp.unreachcall():
            yield (KleeTool(self._options), None, 333)
            yield (KleeTool(self._options), None, 333)

    def name(self):
        return 'klee'

    def executable(self):
        raise NotImplementedError("This should be never called")

    def llvm_version(self):
        # we suppose that all tools
        return self.klee.llvm_version()

    def set_environment(self, env, opts):
        self._env = env
        self.klee.set_environment(env, self._options)

    def actions_before_verification(self, symbiotic):
        # link our specific funs
        self._options.linkundef = ['verifier']
        symbiotic.link_undefined(only_func=['__VERIFIER_assume','__VERIFIER_assert', '__VERIFIER_assume_or_assert'])
        self._options.linkundef = []

    def passes_before_slicing(self):
        if self._options.property.termination():
            return ['-find-exits']
        return []

    def actions_after_slicing(self, cc):
        if self._options.phase == 2:
            output = '{0}-inv.bc'.format(cc.curfile[:cc.curfile.rfind('.')])
            cmd = ['clam.py', '--crab-inter', '--crab-opt=add-invariants', '--crab-opt-invariants-loc=loop-header', '--crab-track=mem', '--crab-widening-delay=32', cc.curfile, '-o', output]
            runcmd(cmd)
            cc.curfile = output

    def passes_after_slicing(self):
        passes = []
        
        if self._options.phase == 1:
            passes = ['-kind-base-case', '-kind-max-backedge-count=8']
        elif self._options.phase == 2:
            passes = ['-kind-step-case', '-kind-k=4']

        # for the memsafety property, make functions behave like they have
        # side-effects, because LLVM optimizations could remove them otherwise,
        # even though they contain calls to assert
        if self._options.property.memsafety():
            passes.append('-remove-readonly-attr')
        elif self._options.property.termination():
            passes.append('-instrument-nontermination')
            passes.append('-instrument-nontermination-mark-header')
        self._options.phase += 1

        return super().passes_after_slicing() + passes

    def replay_error_params(self, llvmfile):
        raise NotImplementedError("This should be never called")

    def cmdline(self, executable, options, tasks, propertyfile=None, rlimits={}):
        raise NotImplementedError("This should be never called")

    def determine_result(self, returncode, returnsignal, output, isTimeout):
        raise NotImplementedError("This should be never called")

    def verifier_failed(self, verifier, res, watch):
        """
        Register that a verifier failed (so that subsequent verifiers can
        learn what happend
        """
        if 'EPTHREAD' in res:
            self._hit_threads = True
