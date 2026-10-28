// EXPECT: Semantic error at line 5: undeclared identifier 'q'
int main(void)
{
    int x = 1;
    x = q + 1;
    return 0;
}
