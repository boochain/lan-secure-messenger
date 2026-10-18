/* C core unit checks against the Rust reference vectors (lan-core/tests/data/[name].json).
 *   cc -std=gnu99 -Isrc -DLSM_APP_VERSION='"test"' -o build/c_unit_test tests/c_unit_test.c -lpthread
 *   build/c_unit_test ../../lan-core/tests/data/filename_vectors.json
 * Includes core.c directly to reach its static helpers. */
#include "../src/core.c"

static const char *js_str(const char *p, char *out, size_t n){ size_t o=0; if(*p!='"') return NULL; p++;
    while(*p && *p!='"'){ unsigned cp; if(*p=='\\'){ p++;
            switch(*p){ case 'n': cp='\n'; break; case 't': cp='\t'; break; case 'r': cp='\r'; break; case 'b': cp=8; break; case 'f': cp=12; break;
                case 'u': { char h[5]={p[1],p[2],p[3],p[4],0}; cp=(unsigned)strtoul(h,NULL,16); p+=4; break; }
                default: cp=(unsigned char)*p; }
            p++;
            if(cp<0x80){ if(o+1<n) out[o++]=(char)cp; }
            else if(cp<0x800){ if(o+2<n){ out[o++]=(char)(0xC0|(cp>>6)); out[o++]=(char)(0x80|(cp&63)); } }
            else { if(o+3<n){ out[o++]=(char)(0xE0|(cp>>12)); out[o++]=(char)(0x80|((cp>>6)&63)); out[o++]=(char)(0x80|(cp&63)); } }
        } else { if(o+1<n) out[o++]=*p; p++; } }
    out[o]=0; return *p=='"'?p+1:NULL; }

int main(int argc, char **argv){ FILE *f; static char buf[1<<20]; size_t len; const char *p; int n=0, bad=0;
    if(argc<2){ fprintf(stderr,"usage: %s filename_vectors.json\n",argv[0]); return 2; }
    f=fopen(argv[1],"rb"); if(!f){ perror(argv[1]); return 2; } len=fread(buf,1,sizeof buf-1,f); buf[len]=0; fclose(f);
    p=buf;
    while((p=strchr(p,'['))){ char in[4096], want[4096], got[4096]; const char *q=p+1;
        while(*q==' '||*q=='\n'||*q=='\r') q++;
        if(*q!='"'){ p=q; continue; }
        q=js_str(q,in,sizeof in); if(!q) break; while(*q==','||*q==' '||*q=='\n'||*q=='\r') q++;
        q=js_str(q,want,sizeof want); if(!q) break;
        sanitize_filename(in,got,sizeof got); n++;
        if(strcmp(got,want)){ bad++; printf("MISMATCH sanitize_filename(\"%s\")\n   C:    \"%s\"\n   Rust: \"%s\"\n",in,got,want); }
        p=q; }
    printf("sanitize_filename: %d vectors, %d mismatches\n",n,bad);
    return (n<10||bad)?1:0; }
