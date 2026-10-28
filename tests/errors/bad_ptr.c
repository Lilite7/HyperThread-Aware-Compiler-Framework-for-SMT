// EXPECT: Unsupported construct at line 5: pointer dereference
int main(void)
{
    int x = 1;
    x = *x;
    return 0;
}
