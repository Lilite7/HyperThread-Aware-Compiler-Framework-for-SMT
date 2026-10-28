// EXPECT: Unsupported construct at line 6: switch statement
#include <stdio.h>
int main(void)
{
    int x = 3;
    switch (x) { }
    return 0;
}
