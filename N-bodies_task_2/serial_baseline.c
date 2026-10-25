/* Independent sequential baseline: same pair law, Euler order, dt and CSV format. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
typedef struct {double m,x,y,vx,vy;} Body;
typedef struct {double fx,fy;} Force;
static void row(FILE *f,double t,Body *b,int n){
    fprintf(f,"%lf",t);
    for(int i=0;i<n;i++)fprintf(f,";%lf;%lf",b[i].x,b[i].y);
    fputc('\n',f);
}
int main(int argc,char **argv){
    if(argc!=3)return 1;
    FILE *in=fopen(argv[1],"r");if(!in)return 1;
    int n;if(fscanf(in,"%d",&n)!=1||n<1){fclose(in);return 1;}
    double end=strtod(argv[2],NULL);if(!isfinite(end)||end<0){fclose(in);return 1;}
    Body *b=calloc((size_t)n,sizeof(*b));Force *f=calloc((size_t)n,sizeof(*f));
    if(!b||!f){free(b);free(f);fclose(in);return 1;}
    for(int i=0;i<n;i++){
        double z,vz;
        if(fscanf(in,"%lf%lf%lf%lf%lf%lf%lf",&b[i].m,&b[i].x,&b[i].y,&z,&b[i].vx,&b[i].vy,&vz)!=7){free(b);free(f);fclose(in);return 1;}
    }
    fclose(in);FILE *out=fopen("output_coordinates.csv","w");
    if(!out){free(b);free(f);return 1;}
    int status=0;double t=0;row(out,t,b,n);
    while(t<end){
        double h=fmin(3600,end-t);memset(f,0,(size_t)n*sizeof(*f));
        for(int i=0;i<n;i++)for(int j=i+1;j<n;j++){
            double dx=b[j].x-b[i].x,dy=b[j].y-b[i].y,r=sqrt(dx*dx+dy*dy);
            if(r==0){status=1;goto cleanup;}
            double fx=6.67430e-11*b[i].m*b[j].m*dx/(r*r*r);
            double fy=6.67430e-11*b[i].m*b[j].m*dy/(r*r*r);
            f[i].fx+=fx;f[i].fy+=fy;f[j].fx-=fx;f[j].fy-=fy;
        }
        for(int i=0;i<n;i++){
            b[i].x+=b[i].vx*h;b[i].y+=b[i].vy*h;
            b[i].vx+=f[i].fx*h/b[i].m;b[i].vy+=f[i].fy*h/b[i].m;
        }
        t+=h;row(out,t,b,n);
    }
cleanup:
    if(ferror(out))status=1;
    if(fclose(out))status=1;
    free(b);free(f);return status;
}
