#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "mosco.c"
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(int argc,char**argv){
    int w=atoi(argv[1]),h=atoi(argv[2]),nseeds=atoi(argv[3]); long B=atol(argv[4]);
    game_params p={w,h}; int seed,ok=0,gave=0,none=0; double tt=0; long totn=0;
    for(seed=1;seed<=nseeds;seed++){
        char sb[32],*aux,*desc; random_state*rs; int n=w*h,k=2*w+2*h+4; int*vals=snewn(k,int);
        sprintf(sb,"%d",seed*7919+w*31+h); rs=random_new(sb,strlen(sb));
        desc=new_game_desc(&p,rs,&aux,false); parse_clues(&p,desc,vals);
        struct mosco_clues*cl=clues_new(w,h,vals); signed char*sol=snewn(n,signed char);
        long budget=B; double t0=now();
        struct msolver*sv=msolver_new(cl); struct sstate*ss=ss_new(sv);
        int r; if(!ss_propagate(sv,ss)) r=0; else r=ss_search(sv,ss,NULL,sol,&budget,0);
        tt+=now()-t0; totn+=B-budget;
        if(r==1)ok++; else if(r<0)gave++; else none++;
        ss_free(ss);msolver_free(sv);clues_free(cl);sfree(vals);sfree(sol);sfree(desc);sfree(aux);random_free(rs);
    }
    printf("%dx%d budget %ld: found %d gave up %d none %d; avg nodes %.0f time %.3fs/puzzle\n",w,h,B,ok,gave,none,(double)totn/nseeds,tt/nseeds);
    return 0;
}
