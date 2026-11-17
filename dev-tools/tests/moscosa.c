/* experiment: simulated annealing for an arrangement of the clues (no givens) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "mosco.c"
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static unsigned long long rngs=88172645463325252ULL;
static unsigned rnd(void){rngs^=rngs<<13;rngs^=rngs>>7;rngs^=rngs<<17;return (unsigned)(rngs>>16);}
static double rndf(void){return (rnd()&0xffffff)/16777216.0;}

/* returns number of moves used, or -1 */
static long sa(const struct mosco_clues*cl,signed char*dir,double T,long maxmoves)
{
    int n=cl->n,k=cl->k,i,d;
    int*cnt=snewn(k,int); long m;
    int cost=0;
    for(i=0;i<n;i++)dir[i]=i%NDIR;
    for(i=n-1;i>0;i--){int j=rnd()%(i+1);signed char t=dir[i];dir[i]=dir[j];dir[j]=t;}
    for(i=0;i<k;i++)cnt[i]=0;
    for(i=0;i<n;i++)cnt[cl->target[i*NDIR+dir[i]]]++;
    for(i=0;i<k;i++)cost+=abs(cnt[i]-cl->clue[i]);
    for(m=0;m<maxmoves&&cost>0;m++){
        int a=rnd()%n,b=rnd()%n; int da=dir[a],db=dir[b];
        int ka0,ka1,kb0,kb1,delta;
        if(da==db)continue;
        ka0=cl->target[a*NDIR+da];ka1=cl->target[a*NDIR+db];
        kb0=cl->target[b*NDIR+db];kb1=cl->target[b*NDIR+da];
        delta=-abs(cnt[ka0]-cl->clue[ka0]);
        /* apply */
        cnt[ka0]--;cnt[ka1]++;cnt[kb0]--;cnt[kb1]++;
        /* recompute touched */
        {int ks[4]={ka0,ka1,kb0,kb1};int u[4],nu=0,x,y;
         for(x=0;x<4;x++){for(y=0;y<nu;y++)if(u[y]==ks[x])break;if(y==nu)u[nu++]=ks[x];}
         /* old cost of touched: undo to compute */
         int oldc=0,newc=0;
         for(x=0;x<nu;x++)newc+=abs(cnt[u[x]]-cl->clue[u[x]]);
         cnt[ka0]++;cnt[ka1]--;cnt[kb0]++;cnt[kb1]--;
         for(x=0;x<nu;x++)oldc+=abs(cnt[u[x]]-cl->clue[u[x]]);
         delta=newc-oldc;
         if(delta<=0||rndf()<exp(-delta/T)){
            cnt[ka0]--;cnt[ka1]++;cnt[kb0]--;cnt[kb1]++;
            dir[a]=db;dir[b]=da;cost+=delta;
         }
        }
    }
    sfree(cnt);
    return cost==0?m:-1;
}
int main(int argc,char**argv){
    int w=atoi(argv[1]),h=atoi(argv[2]),nseeds=atoi(argv[3]);double T=atof(argv[4]);long maxm=atol(argv[5]);
    game_params p={w,h};int seed,ok=0;long tot=0;double t0=now();
    for(seed=1;seed<=nseeds;seed++){
        char sb[32],*aux,*desc;random_state*rs;int n=w*h,k=2*w+2*h+4;int*vals=snewn(k,int);
        sprintf(sb,"%d",seed*7919+w*31+h);rs=random_new(sb,strlen(sb));
        desc=new_game_desc(&p,rs,&aux,false);parse_clues(&p,desc,vals);
        struct mosco_clues*cl=clues_new(w,h,vals);signed char*dir=snewn(n,signed char);
        long r=sa(cl,dir,T,maxm);
        if(r>=0){ok++;tot+=r;
          /* verify */ int*cnt=snewn(k,int),used[NDIR];count_arrows(cl,dir,cnt,used);int i;for(i=0;i<k;i++)if(cnt[i]!=cl->clue[i])printf("BAD\n");for(i=0;i<NDIR;i++)if(used[i]!=n/NDIR)printf("BADCLASS\n");sfree(cnt);}
        clues_free(cl);sfree(vals);sfree(dir);sfree(desc);sfree(aux);random_free(rs);
    }
    printf("%dx%d T=%.2f: solved %d/%d avg moves %.0f time/puzzle %.3fs\n",w,h,T,ok,nseeds,ok?(double)tot/ok:0.0,(now()-t0)/nseeds);
    return 0;}
