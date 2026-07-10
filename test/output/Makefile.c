#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define M 3
static const unsigned char D0[575]="!=?KT,@__JAs-kH7Vlc9'5_Z!%pWI^A<Lb],?%[,!.kV,A0sF&F,L$]A<`0gCm'9<!.=7o5UKvQ4@T<S;HP%o0U#r;!,U^+E@,+gCo(Kh,?$v_@WlOu!)i;#7ODJn;L8Co7Vc.=2-)DB!'.F-%5qo!#CQr2%=1<j6Z#8C!e8rVE0cMsBUNq#!#)?U9-wG^>[R]/!IN]f!,hTW,:7MrG+&u$%42Ww6Uj>$#$F2m%57vG6XV$v#@eb<AiXNC-Ve$J(Jc$2#3%'a,?&MT!%lhQ-[K)N(+OS^5:6Me4YR0pFdwrO(Q8TN!dw)/!r+r7!Z$^p/LFFU#Uc0^IlO&=BShDn6Uj8N!2BAPEK'sN/hhi*0jT>O%4aKdE@,e%E/+FZEDC[/EgXb3!%IW3F!22_(5tXo?V!ko!-RJk?RB$I?Uu/u5!9QEBUX(9!-bq64tiDP,u8#Q#,v=+6_Dlh!S>Mq1+`TI*@i0p!MA*3;l%la/i-UJ!;Lv,%-o0t,V$?F!=M9P!.Efa,<#_A4XRUAQW4?(!7-+B+F=/V#IXa=#bn!v!B5UN!=FnX!-wAhFt_e`,Y.op";
static const char*N[]={"test/input/Makefile",};
static const size_t S[]={586,};
static const size_t C[]={460,};
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
