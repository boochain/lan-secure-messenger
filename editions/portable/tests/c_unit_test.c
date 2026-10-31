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

static int hist_tests(void){ lsm_msg m[4]; lsm_hq q; int bad=0; memset(m,0,sizeof m);
    m[0].kind=K_TEXT; m[0].body="\xe4\xbd\xa0\xe5\xa5\xbd World"; strcpy(m[0].conv,"dm:a"); m[0].ts_ms=100; m[0].out=1;
    m[1].kind=K_FILE; m[1].body=""; strcpy(m[1].file_name,"heart.png"); strcpy(m[1].conv,"dm:b"); m[1].ts_ms=200;
    m[2].kind=K_FILE; m[2].body=""; strcpy(m[2].file_name,"Photo.JPG"); strcpy(m[2].conv,"dm:a"); m[2].ts_ms=300;
    m[3].kind=K_SYSTEM; m[3].body="world"; strcpy(m[3].conv,"dm:a");
    memset(&q,0,sizeof q); q.query="\xe5\xa5\xbd"; bad+=!lsm_history_match(&m[0],&q);
    q.query="WORLD"; bad+=!lsm_history_match(&m[0],&q); bad+=lsm_history_match(&m[3],&q);
    memset(&q,0,sizeof q); q.types=1<<LSM_T_STICKER; bad+=!lsm_history_match(&m[1],&q); bad+=lsm_history_match(&m[2],&q);
    bad+=lsm_msg_type(&m[2])!=LSM_T_IMAGE;
    memset(&q,0,sizeof q); q.convs="dm:x,dm:a"; q.direction=2; bad+=!lsm_history_match(&m[0],&q); bad+=lsm_history_match(&m[2],&q);
    memset(&q,0,sizeof q); q.from_ms=100; q.to_ms=200; bad+=!lsm_history_match(&m[0],&q); bad+=lsm_history_match(&m[1],&q);
    bad+=!lsm_is_sticker_name("lsm-sticker-cat.gif");
    printf("history_query: %d failures\n",bad); return bad; }

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
    bad+=hist_tests();
    return (n<10||bad)?1:0; }
