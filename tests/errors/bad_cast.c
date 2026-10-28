// EXPECT: Unsupported construct at line 5: type cast
int main(void)
{
    double d = 2.5;
    int x = (int)d;
    return x;
}
