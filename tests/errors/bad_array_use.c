// EXPECT: array 'a' used without subscript
int a[10];
int main(void)
{
    int x;
    x = a + 1;
    return 0;
}
