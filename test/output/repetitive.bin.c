#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define M 3
static const unsigned char D0[1515]="!=?L+F.WH)CmN*[G=EwB!&C%H1F=ld,A.!KC6^oo;k`Kj!,hZ`0e[6rCn4q`,FsO#GE*g=!-n`e,:7SpFdQN`GE6YpEF9oq7ldp;1haCd2.&LS4C_dg4C_6]5]a2i5[?BcLL)vU4CA4hNF;nM4XbjI4XbnL1hhBK1hhBLMdVrnCKEPMCq#L*!duHO!e+UI4Xc,R4Xc,H2.6Xe4Xc,R-!$fB2.-KR2+Tc^Ddg%RDdf2s!e1WR!e1W:4Xc,S4Xc,SV-k@n2IQj*4Xc-S2IHTS2IHTSMdj(pDdp+SDmun/!e1ZS!e4[M4Xc,T4Xc-S2ed^T2en$E-!$fB2ed^T2c5u`De#1TDe!>u!e1^T!e1a=4Xc,U4Xc,U-!$fB3+)gU33YJ))INW73+)gUMe&4rDe,7UDqC4Q!e8nIJPn$6N[5190./J]tulMY1H61%&Yr-H:bHUPX^PU6tulL4#0?h+J_?,)E$Ywq1bc45&YvF6!C1thJQ;qXtulMQ1bn0LtulNY+<nh^+Vu)Lt&'>EtwDu;1d.KL+<jNn3[GerX^>I4twDsn&uJuot3NJ`H6kR]1bc45+<nOd!C*g8+<jNn3RIeut&Ta+twDsm!C.vY+<j?u;'d_QtulMY,ZL*^/ubqYgHj&GX^Q#lu!tDN&uJv(IF'^0%OEX'1bc45/ug4N&2=,eI9-GZu!tEs1d#Lk/ubqO3[H;SX^%:9u$Ll+&SM,Urq6w2(bTZntulL+6>3<)u$Ll0&SD&Trq7&Y%OF._1bc454W_V0&2=,srd<g.u$LmT1d#Lk4WZ=13[Hh5X^.l4u&&<c&SM,eH-f/tBI-3E2)ITM9:W!h&8rM0JhCp-.<Y(1u&&<i4_A#0GvlG6XtHW61bc459:R_u%FG,uGvl#Vu&&<g*CHlY=sJpFAhLM%0LY$Q2'm<I1haHv=sODI&2=-:qK%B*u'Ten1eQtL=sK+J3[G9:XtFRY.6$9<=sK/<6eYkdqK%jAu&&?69:W%69:Q^]?6r,!u!tEs.$'-7BUCPf_*OeKXtG);twDsWE<vStqX&rnqIw@=u&&<i!^VNR9:Z$)Gu]FOu).6O0j(c!4WbW8rbPd+XtGTsu!tDE4Q-62t3Ue;t%Nv?u*][c+69IRG8;p)`Bk:E1bc35u).7OBUC/.MBv=IXtI-n-E/=C&Z#FhJ_FEYJNS[,u,6-E+69IR&Z#FhJhCp-+@<gpu).7OBUC/Ktt7%mXtI-n.Pr5Y9:Z$)H-en;V_4AMu$LkrQ?9/YKq4>bKI)av1d#Kku-eT&!C1v1E[?=,X]B>$tulNY&Z#FhJ_>B#W$RQD2.1PX&YvHY&Z#FhJN^[Iu-eUJu-eSm5D9']D-.=DXtJ[iu,6-9%Kd[SD9u%k4tc:lu*][X&SA'qBUJh`F]NlAu'Tenu'TfnYnrW*j$EvEXtJ[iu,6-*'Sm_2H-mS6GuqG*u'Tfn=sOD?+CP4^s$=BM.Q%!Ru-eVJPS,?*sZw/dXtJ[i-gX)9G8C4Op?]TG$708U2*tXmBUAdO";
static const char*N[]={"test/input/repetitive.bin",};
static const size_t S[]={49460,};
static const size_t C[]={1210,};
static const unsigned char*P[]={D0,};
static int F=1;
static void x86(unsigned char*d,size_t n){
for(size_t i=0;i+4<n;i++)
if((d[i]==0xE8||d[i]==0xE9)&&(d[i+4]==0||d[i+4]==0xFF)){
unsigned a=(unsigned)d[i+1]|(unsigned)d[i+2]<<8|(unsigned)d[i+3]<<16;
a-=i;d[i+1]=a;d[i+2]=a>>8;d[i+3]=a>>16;i+=4;}}
static void lz(const unsigned char*i,size_t n,unsigned char*e){
int flg=i[0];if(flg==0){memcpy(e,i+1,n-1);return;}
int e8=flg==2;size_t p=1,q=0;
while(p<n){unsigned char f=i[p++];
for(int b=0;b<4&&p<n;b++){int t=f&3;f>>=2;
if(t==0)e[q++]=i[p++];
else{int L=i[p++]+M;
if(t==1){int o=i[p++];for(int j=0;j<L;j++)e[q+j]=e[q+j-o];q+=L;}
else{int o=(i[p]<<8)|i[p+1];p+=2;for(int j=0;j<L;j++)e[q+j]=e[q+j-o];q+=L;}}}}
if(e8)x86(e,q);}
static int v85(unsigned char c){return c=='!'?0:c<='['?c-34:c-35;}
static void b8(const unsigned char*i,size_t n,unsigned char*e){
size_t p=0,q=0,w;while(p+4<n){
w=0;for(int j=0;j<5;j++)w=w*85+v85(i[p++]);
e[q++]=w>>24;e[q++]=w>>16;e[q++]=w>>8;e[q++]=w;}
if(p<n){w=0;int j;for(j=0;p+j<n;j++)w=w*85+v85(i[p+j]);
for(;j<5;j++)w=w*85+84;for(j=3-(n-p)%5;j<4;j++)e[q++]=w>>(24-j*8);}}
int main(){
for(int i=0;i<F;i++){
size_t cl=(C[i]+3)/4*4;
unsigned char*c=malloc(cl);if(!c)return 1;
b8(P[i],(C[i]+3)/4*5,c);
unsigned char*b=malloc(S[i]);if(!b){free(c);return 1;}
lz(c,C[i],b);free(c);
FILE*f=fopen(N[i],"wb");if(!f){free(b);return 1;}
fwrite(b,1,S[i],f);fclose(f);free(b);puts(N[i]);}
return 0;}
