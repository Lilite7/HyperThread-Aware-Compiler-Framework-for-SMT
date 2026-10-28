// EXPECT: array subscript must be an integer
int a[10];
int main(void)
{
    double d = 1.0;
    a[d] = 3;
    return 0;
}
