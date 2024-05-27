extern void abort(void) __attribute__((noreturn));
extern void __VERIFIER_assert(int expr);
void klee_silent_exit(int status);

void __VERIFIER_assume(int expr)
{
	if (!expr)
		klee_silent_exit(0);
}

void __VERIFIER_assert_or_assume(int expr, _Bool flag)
{
	if (flag)
	  __VERIFIER_assume(expr);
	else
	  __VERIFIER_assert(expr);
}
