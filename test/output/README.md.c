#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define M 3
static const unsigned char D0[1350]="!%bVQFdC<6kff8:<g:eX0Pcq,FcbJ#Cn8R,7Rg]9As_T#BUK2e7$rVW,EHo?A4C'wE0cO-G*w'7ECP&?EgrQ=CPR!+Ate.jGDhN:/PqMODj?92H_]v8AtwG'C.:QcG*R3BAs@emFcteuH&HaF/Q<(.CPt=*GbaTGG=H;>,DRD7Cn8S#BLZB6AriFmCn&@(GE-+*,Dq&pEh(I7BUNr)G!,p0B1?q)G+*?ABUKw:CPt=*FdU1$EJJ@)Bpe(oG!-]@BTtX$Eh(I.Dj8.#A<_UwFen8Q%5:cHBLZF&As_T#BLZU!Dj5b(G!-,=A=-fqGDh$q,FWOFAW%[4/X10JDij@uBUC8N,FW;+ECP&8BLZq4CmN.vA<J'wCn&A&,Dh?-AWL`mB2g3t-:p>3G)'_o%5;1L@wqwiAs.X6E_kQ#Eh(I7BUNq9AtI5(Fd1=60p7#O.7PqqAs1GqF-qO3G!,v3E0cO-G*tuE,BI:]BT-YoBLXSD,A2%^Ea?O-As.X6E_kW:GF2h#G=FmkC6_E5,>9FY0j`f^GF2h#G;*Eb@l]7@CPt<tEh(I7BUNr3F[ee=As1GqF-qO3G(rG?Fd0[kG!,p0Hr&!JE/8(u,F)l8Cn8o5G=H21Dj8[2A<_UwF-qn.G`q!),DJLVC6_E5@pNfeCPt=-BUO!2GE6X+,AA9bCn&@(CndE-Eh(I.Dj8.#A<_UwBV&etGba3+,FWOFFd142Eg,!$BL[*/BLZq4CmN.vA<J'wCn&A&0fas2-:UOsH:DVB,FscFDOCik1gPSw/TP$*3_wVLE0cO-G*w'7EDS,ZHB305,DRD&FenK90T9rBAs@f#A=@!o,E-?+B8_f)G)9XtEK>!%,DU/24)nS^HB2t+Ei*PE0i*Wp7>GsI4'NikEJ]UsCn8R6/OkZB,FW;/,Dh?-F-qO3G)Kn*G+*H7A<S.1G)qO*,DRC]0U[$TCn8R9G)'[n,DU*+CPQA_G=Ew@H'N*+G!,l8@o'nsB1?9d@owG>0i,--BLZNrAtHojF[ee=DlLK4/l0604,,1YCndE/E/8(gB8_:qCn6'9CPt=!BTw)#A=@(m,AA9bCn&?h%7re=<JuklCn]f&BTwV3%59A.6r0OU4@WiNE0c4+BUBpB8ohXr,ACuSEK)MD:kH)]01Jq,EM#U;/2/YM=,qjeC5ZunFcc(%B1?=%As_T#BLZ_/,Fj21Cm0?S/m<bTEhRv8ELLI2F-qO4BUBq8Gb<<=FebFQ7ZrJTC/Q0oCn9$B%59#',A;4dDj/7vC+pwS@up&j%?l4gBJ<iR@l]6e-:UtuG+*-7C+pwS@up&j%?l4gBL[*,G+&cu@ul=L-:p>*Cm)`nG)H]e:jB;m";
static const char*N[]={"test/input/README.md",};
static const size_t S[]={1079,};
static const size_t C[]={1080,};
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
