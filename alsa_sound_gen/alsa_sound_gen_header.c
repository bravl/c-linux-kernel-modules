#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_LEN 8000

const char header_str[] = "const char music[] = {\n";
const char magic_str[] = "0xDE, 0xAD, 0xBE, 0xEF,\n";
const char end_str[] = "};\n";

int main()
{
        int i = 0;
        unsigned char t = 0;
        FILE *fp;
        fp = fopen("music_gen.h", "w");
        if (!fp) return -1;

        fwrite(header_str, sizeof(char), strlen(header_str), fp);
        for (int i = 0; i < (SAMPLE_LEN); i++) {
                fprintf(fp, "0x%x,",t);
                if (!(i % 15)) fprintf(fp,"\n");
                t++;
                t++;
        } 
        fwrite(end_str, sizeof(char), strlen(end_str), fp);
        fclose(fp);
}
