"""
Test lldb target stop-hook mechanism to see whether it fires off correctly .
"""

import os
import unittest2
import lldb
from lldbtest import *

class StopHookMechanismTestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @skipUnlessDarwin
    @dsym_test
    def test_with_dsym(self):
        """Test the stop-hook mechanism."""
        self.buildDsym()
        self.stop_hook_firing()

    @skipIfFreeBSD # llvm.org/pr15037
    @expectedFailureLinux('llvm.org/pr15037') # stop-hooks sometimes fail to fire on Linux
    @expectedFailureWindows("llvm.org/pr22274: need a pexpect replacement for windows")
    @dwarf_test
    def test_with_dwarf(self):
        """Test the stop-hook mechanism."""
        self.buildDwarf()
        self.stop_hook_firing()

    def setUp(self):
        # Call super's setUp().
        TestBase.setUp(self)
        # Find the line numbers inside main.cpp.
        self.begl = line_number('main.cpp', '// Set breakpoint here to test target stop-hook.')
        self.endl = line_number('main.cpp', '// End of the line range for which stop-hook is to be run.')
        self.correct_step_line = line_number ('main.cpp', '// We should stop here after stepping.')
        self.line = line_number('main.cpp', '// Another breakpoint which is outside of the stop-hook range.')

    def stop_hook_firing(self):
        """Test the stop-hook mechanism."""
        import pexpect
        exe = os.path.join(os.getcwd(), "a.out")
        prompt = "(lldb) "
        add_prompt = "Enter your stop hook command(s).  Type 'DONE' to end."
        add_prompt1 = "> "

        # So that the child gets torn down after the test.
        self.child = pexpect.spawn('%s %s %s' % (self.lldbHere, self.lldbOption, exe))
        child = self.child
        # Turn on logging for what the child sends back.
        if self.TraceOn():
            child.logfile_read = sys.stdout

        # Set the breakpoint, followed by the target stop-hook commands.
        child.expect_exact(prompt)
        child.sendline('breakpoint set -f main.cpp -l %d' % self.begl)
        child.expect_exact(prompt)
        child.sendline('breakpoint set -f main.cpp -l %d' % self.line)
        child.expect_exact(prompt)
        child.sendline('target stop-hook add -f main.cpp -l %d -e %d' % (self.begl, self.endl))
        child.expect_exact(add_prompt)
        child.expect_exact(add_prompt1)
        child.sendline('expr ptr')
        child.expect_exact(add_prompt1)
        child.sendline('DONE')
        child.expect_exact(prompt)
        child.sendline('target stop-hook list')

        # Now run the program, expect to stop at the the first breakpoint which is within the stop-hook range.
        child.expect_exact(prompt)
        child.sendline('run')
        # Make sure we see the stop hook text from the stop of the process from the run hitting the first breakpoint
        child.expect_exact('(void *) $')
        child.expect_exact(prompt)
        child.sendline('thread step-over')
        # Expecting to find the output emitted by the firing of our stop hook.
        child.expect_exact('(void *) $')
        # This is orthogonal to the main stop hook test, but this example shows a bug in
        # CLANG where the line table entry for the "return -1" actually includes some code
        # from the other branch of the if/else, so we incorrectly stop at the "return -1" line.
        # I fixed that in lldb and I'm sticking in a test here because I don't want to have to
        # make up a whole nother test case for it.
        child.sendline('frame info')
        at_line = 'at main.cpp:%d' % (self.correct_step_line)
        print 'expecting "%s"' % at_line
        child.expect_exact(at_line)

        # Now continue the inferior, we'll stop at another breakpoint which is outside the stop-hook range.
        child.sendline('process continue')
        child.expect_exact('// Another breakpoint which is outside of the stop-hook range.')
        #self.DebugPExpect(child)
        child.sendline('thread step-over')
        child.expect_exact('// Another breakpoint which is outside of the stop-hook range.')
        #self.DebugPExpect(child)
        # Verify that the 'Stop Hooks' mechanism is NOT BEING fired off.
        self.expect(child.before, exe=False, matching=False,
            substrs = ['(void *) $'])
        

if __name__ == '__main__':
    import atexit
    lldb.SBDebugger.Initialize()
    atexit.register(lambda: lldb.SBDebugger.Terminate())
    unittest2.main()
