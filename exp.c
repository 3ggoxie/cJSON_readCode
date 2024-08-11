#include <stdio.h>

void printCountdown(int start) // switch语句的贯穿特性
{
    switch (start)
    {
    case 5:
        printf("5\n");
    case 4:
        printf("4\n");
    case 3:
        printf("3\n");
    case 2:
        printf("2\n");
    case 1:
        printf("1\n");
        break;
    default:
        printf("请输入 1 到 5 之间的数字。\n");
        break;
    }
}
int main(int argc, char const *argv[])
{

    // char *a = "\"sdfas";
    // // int i = 0;
    // // while (a[i] != '\0')
    // // {
    // //     printf("%c", a[i]);
    // //     ++i;
    // // }
    // if (*a == '"')
    // {
    //     printf("\"\n");
    // }
    // if (*a == '\"')
    // {
    //     printf("\\\"\n");
    // }

    int start;
    printf("请输入 1 到 5 之间的数字:");
    scanf("%d", &start);

    printCountdown(start);

    return 0;
}
