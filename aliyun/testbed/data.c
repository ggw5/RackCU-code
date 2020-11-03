#include <stdio.h>
#include <stdlib.h>

int main()
{
    char str[11]= "0123456789";

    FILE *fp;
    fp=fopen("data_file","w");

    int i,j,k,t;
    int size=4;
    int temp;
    for(i=0;i<1024;i++)
    {
        for(j=0;j<1024;j++)
        {
            for(k=0;k<1024;k++)
            {
                for(t=0;t<size;t++)
                {
                    temp=rand()%10;
                    fputc(str[temp],fp);
                }
            }
        }
    }
    fclose(fp);
    return 0;
}
