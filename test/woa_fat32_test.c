#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "woa_fat32.h"
static FILE *f;
static int wr(void *c, u32 lba, u32 cnt, const void *b){ fseek(f,(long long)lba*512,SEEK_SET); return fwrite(b,512,cnt,f)!=cnt; }
int main(int argc,char**argv){
  u32 start=atoi(argv[1]), sct=atoi(argv[2]);
  f=fopen(argv[3],"r+b");
  static u8 z[128*512];
  int r=woa_fat32_format(wr,NULL,start,sct,0x1234ABCD,z,128);
  printf("rc=%d\n",r); fclose(f); return r;
}
