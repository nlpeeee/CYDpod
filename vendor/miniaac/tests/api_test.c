#include "miniaac.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"check failed line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static int fail_read(void *u,uint64_t at,void *p,size_t n) {
    (void)u;(void)at;(void)p;(void)n;return -1;
}
int main(void) {
    size_t n=miniaac_workspace_size(),count=9;
    void *arena=malloc(n+8); miniaac *d=(miniaac *)1;
    miniaac_source s={fail_read,NULL,100};
    CHECK(n+4096<=48*1024);
    CHECK(miniaac_open(arena,n-1,&s,&d)==MINIAAC_MEMORY && d==NULL);
    CHECK(miniaac_open((char *)arena+1,n,&s,&d)==MINIAAC_ARGUMENT);
    CHECK(miniaac_open(arena,n,&s,&d)==MINIAAC_IO && d==NULL);
    CHECK(miniaac_open(arena,n,NULL,&d)==MINIAAC_ARGUMENT);
    CHECK(miniaac_open(arena,n,&s,NULL)==MINIAAC_ARGUMENT);
    CHECK(miniaac_decode_next(NULL,NULL,0,&count)==MINIAAC_ARGUMENT && !count);
    CHECK(miniaac_get_info(NULL,NULL)==MINIAAC_ARGUMENT);
    miniaac_close(NULL);free(arena);
    printf("API checks passed; workspace=%zu, PCM=4096, total=%zu bytes\n",n,n+4096);
    return 0;
}
