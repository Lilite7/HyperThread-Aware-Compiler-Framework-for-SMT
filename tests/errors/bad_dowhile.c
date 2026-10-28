// EXPECT: Unsupported construct at line 5: do-while loop
int main(void)
{
    int i = 0;
    do { i++; } while (i < 3);
    return 0;
}
