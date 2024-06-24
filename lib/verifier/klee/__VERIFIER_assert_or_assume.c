extern void __VERIFIER_assume(int);
extern void __VERIFIER_assert(int);

void __VERIFIER_assert_or_assume(int expr, int flag)
{
	if (flag)
		__VERIFIER_assume(expr);
	else
		__VERIFIER_assert(expr);
}
