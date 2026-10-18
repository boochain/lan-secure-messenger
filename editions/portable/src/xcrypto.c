#include "xcrypto.h"
#include "third_party/monocypher.h"
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
typedef BOOLEAN (WINAPI *RtlGenRandomFn)(PVOID, ULONG);
#else
#include <stdio.h>
#endif

/* ---------------- BLAKE2s (RFC 7693) ---------------- */
static const uint32_t B2S_IV[8] = {0x6A09E667,0xBB67AE85,0x3C6EF372,0xA54FF53A,0x510E527F,0x9B05688C,0x1F83D9AB,0x5BE0CD19};
static const uint8_t SIG[10][16] = {
 {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},{14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3},
 {11,8,12,0,5,2,15,13,10,14,3,6,7,1,9,4},{7,9,3,1,13,12,11,14,2,6,5,10,4,0,15,8},
 {9,0,5,7,2,4,10,15,14,1,11,12,6,8,3,13},{2,12,6,10,0,11,8,3,4,13,7,5,15,14,1,9},
 {12,5,1,15,14,13,4,10,0,7,6,3,9,2,8,11},{13,11,7,14,12,1,3,9,5,0,15,4,8,6,2,10},
 {6,15,14,9,11,3,0,8,12,2,13,7,1,4,10,5},{10,2,8,4,7,6,1,5,15,11,9,14,3,12,13,0}};
#define ROTR32(x,n) (((x)>>(n))|((x)<<(32-(n))))
static uint32_t ld32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void st32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static void b2s_compress(blake2s_ctx *c, int last){
    uint32_t v[16], m[16]; int i;
    for(i=0;i<8;i++){v[i]=c->h[i];v[i+8]=B2S_IV[i];}
    v[12]^=c->t[0]; v[13]^=c->t[1]; if(last) v[14]=~v[14];
    for(i=0;i<16;i++) m[i]=ld32(c->buf+4*i);
#define G(a,b,cc,d,x,y) v[a]+=v[b]+x; v[d]=ROTR32(v[d]^v[a],16); v[cc]+=v[d]; v[b]=ROTR32(v[b]^v[cc],12); \
    v[a]+=v[b]+y; v[d]=ROTR32(v[d]^v[a],8); v[cc]+=v[d]; v[b]=ROTR32(v[b]^v[cc],7);
    for(i=0;i<10;i++){const uint8_t *s=SIG[i];
        G(0,4,8,12,m[s[0]],m[s[1]]) G(1,5,9,13,m[s[2]],m[s[3]]) G(2,6,10,14,m[s[4]],m[s[5]]) G(3,7,11,15,m[s[6]],m[s[7]])
        G(0,5,10,15,m[s[8]],m[s[9]]) G(1,6,11,12,m[s[10]],m[s[11]]) G(2,7,8,13,m[s[12]],m[s[13]]) G(3,4,9,14,m[s[14]],m[s[15]])}
#undef G
    for(i=0;i<8;i++) c->h[i]^=v[i]^v[i+8];
}
void blake2s_init(blake2s_ctx *c, size_t outlen){
    int i; memset(c,0,sizeof *c); for(i=0;i<8;i++) c->h[i]=B2S_IV[i];
    c->h[0]^=0x01010000u^(uint32_t)outlen; c->outlen=outlen;
}
void blake2s_update(blake2s_ctx *c, const void *in, size_t n){
    const uint8_t *p=(const uint8_t*)in; size_t i;
    for(i=0;i<n;i++){ if(c->c==64){ c->t[0]+=64; if(c->t[0]<64) c->t[1]++; b2s_compress(c,0); c->c=0;} c->buf[c->c++]=p[i]; }
}
void blake2s_final(blake2s_ctx *c, uint8_t *out){
    size_t i; c->t[0]+=(uint32_t)c->c; if(c->t[0]<c->c) c->t[1]++;
    while(c->c<64) c->buf[c->c++]=0;
    b2s_compress(c,1);
    for(i=0;i<c->outlen;i++) out[i]=(uint8_t)(c->h[i>>2]>>(8*(i&3)));
}
void blake2s(uint8_t out[32], const void *in, size_t n){blake2s_ctx c; blake2s_init(&c,32); blake2s_update(&c,in,n); blake2s_final(&c,out);}

static void hmac_b2s(uint8_t out[32], const uint8_t key[32], const uint8_t *d1, size_t n1){
    uint8_t k[64], ih[32]; blake2s_ctx c; int i;
    memset(k,0,64); memcpy(k,key,32);
    for(i=0;i<64;i++) k[i]^=0x36;
    blake2s_init(&c,32); blake2s_update(&c,k,64); blake2s_update(&c,d1,n1); blake2s_final(&c,ih);
    for(i=0;i<64;i++) k[i]^=0x36^0x5c;
    blake2s_init(&c,32); blake2s_update(&c,k,64); blake2s_update(&c,ih,32); blake2s_final(&c,out);
}
/* Noise HKDF with 2 outputs */
static void hkdf2(const uint8_t ck[32], const uint8_t *ikm, size_t n, uint8_t o1[32], uint8_t o2[32]){
    uint8_t tk[32], t[33];
    hmac_b2s(tk, ck, ikm, n);
    t[0]=1; hmac_b2s(o1, tk, t, 1);
    memcpy(t,o1,32); t[32]=2; hmac_b2s(o2, tk, t, 33);
    crypto_wipe(tk,32);
}

/* ---------------- SHA-256 ---------------- */
static const uint32_t K256[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static void sha_block(sha256_ctx *c){
    uint32_t w[64],a,b,cc,d,e,f,g,h,t1,t2; int i;
    for(i=0;i<16;i++) w[i]=((uint32_t)c->buf[4*i]<<24)|((uint32_t)c->buf[4*i+1]<<16)|((uint32_t)c->buf[4*i+2]<<8)|c->buf[4*i+3];
    for(i=16;i<64;i++){uint32_t s0=ROTR32(w[i-15],7)^ROTR32(w[i-15],18)^(w[i-15]>>3), s1=ROTR32(w[i-2],17)^ROTR32(w[i-2],19)^(w[i-2]>>10); w[i]=w[i-16]+s0+w[i-7]+s1;}
    a=c->s[0];b=c->s[1];cc=c->s[2];d=c->s[3];e=c->s[4];f=c->s[5];g=c->s[6];h=c->s[7];
    for(i=0;i<64;i++){t1=h+(ROTR32(e,6)^ROTR32(e,11)^ROTR32(e,25))+((e&f)^(~e&g))+K256[i]+w[i];
        t2=(ROTR32(a,2)^ROTR32(a,13)^ROTR32(a,22))+((a&b)^(a&cc)^(b&cc)); h=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;}
    c->s[0]+=a;c->s[1]+=b;c->s[2]+=cc;c->s[3]+=d;c->s[4]+=e;c->s[5]+=f;c->s[6]+=g;c->s[7]+=h;
}
void sha256_init(sha256_ctx *c){static const uint32_t iv[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}; memcpy(c->s,iv,32); c->n=0; c->c=0;}
void sha256_update(sha256_ctx *c, const void *in, size_t n){
    const uint8_t *p=(const uint8_t*)in;
    while(n){ size_t k=64-c->c; if(k>n)k=n; memcpy(c->buf+c->c,p,k); c->c+=k; p+=k; n-=k; c->n+=k; if(c->c==64){sha_block(c); c->c=0;} }
}
void sha256_final(sha256_ctx *c, uint8_t out[32]){
    uint64_t bits=c->n*8; int i; uint8_t pad=0x80, z=0;
    sha256_update(c,&pad,1); while(c->c!=56) sha256_update(c,&z,1);
    for(i=7;i>=0;i--){uint8_t b=(uint8_t)(bits>>(8*i)); sha256_update(c,&b,1);}
    for(i=0;i<8;i++){out[4*i]=(uint8_t)(c->s[i]>>24);out[4*i+1]=(uint8_t)(c->s[i]>>16);out[4*i+2]=(uint8_t)(c->s[i]>>8);out[4*i+3]=(uint8_t)c->s[i];}
}
void sha256(uint8_t out[32], const void *in, size_t n){sha256_ctx c; sha256_init(&c); sha256_update(&c,in,n); sha256_final(&c,out);}

/* ---------------- ChaCha20-Poly1305 ---------------- */
static void aead_mac(uint8_t tag[16], const uint8_t key[32], const uint8_t nonce[12], const uint8_t *ad, size_t adn, const uint8_t *ct, size_t n){
    uint8_t poly_key[64], zero[16], lens[16]; crypto_poly1305_ctx p;
    memset(poly_key,0,64); memset(zero,0,16);
    crypto_chacha20_ietf(poly_key, poly_key, 64, key, nonce, 0);
    crypto_poly1305_init(&p, poly_key);
    crypto_poly1305_update(&p, ad, adn); if(adn%16) crypto_poly1305_update(&p, zero, 16-adn%16);
    crypto_poly1305_update(&p, ct, n); if(n%16) crypto_poly1305_update(&p, zero, 16-n%16);
    { uint64_t a=adn, b=n; int i; for(i=0;i<8;i++){lens[i]=(uint8_t)(a>>(8*i)); lens[8+i]=(uint8_t)(b>>(8*i));} }
    crypto_poly1305_update(&p, lens, 16);
    crypto_poly1305_final(&p, tag);
    crypto_wipe(poly_key,64);
}
void aead_seal(uint8_t *out, const uint8_t key[32], const uint8_t nonce[12], const uint8_t *ad, size_t adn, const uint8_t *pt, size_t n){
    crypto_chacha20_ietf(out, pt, n, key, nonce, 1);
    aead_mac(out+n, key, nonce, ad, adn, out, n);
}
int aead_open(uint8_t *out, const uint8_t key[32], const uint8_t nonce[12], const uint8_t *ad, size_t adn, const uint8_t *ct, size_t n){
    uint8_t tag[16];
    if(n<16) return -1; n-=16;
    aead_mac(tag, key, nonce, ad, adn, ct, n);
    if(crypto_verify16(tag, ct+n)) return -1;
    crypto_chacha20_ietf(out, ct, n, key, nonce, 1);
    return 0;
}

void rand_bytes(void *p, size_t n){
#ifdef _WIN32
    static RtlGenRandomFn fn; HMODULE h;
    if(!fn){ h=LoadLibraryA("advapi32.dll"); if(h) fn=(RtlGenRandomFn)GetProcAddress(h,"SystemFunction036"); }
    if(!fn || !fn(p,(ULONG)n)){ MessageBoxA(NULL,"RtlGenRandom failed","LanMessenger",MB_ICONSTOP); ExitProcess(3);} 
#else
    FILE *f=fopen("/dev/urandom","rb"); if(!f||fread(p,1,n,f)!=n) abort(); fclose(f);
#endif
}

/* ---------------- Noise XX ---------------- */
static const char PROTO[]="Noise_XX_25519_ChaChaPoly_BLAKE2s";
static void mix_hash(noise_hs *s, const uint8_t *d, size_t n){blake2s_ctx c; blake2s_init(&c,32); blake2s_update(&c,s->h,32); blake2s_update(&c,d,n); blake2s_final(&c,s->h);}
static void mix_key(noise_hs *s, const uint8_t *ikm){uint8_t k[32]; hkdf2(s->ck, ikm, 32, s->ck, k); memcpy(s->k,k,32); s->has_k=1; s->n=0;}
static void nonce12(uint8_t nn[12], uint64_t n){int i; memset(nn,0,4); for(i=0;i<8;i++) nn[4+i]=(uint8_t)(n>>(8*i));}
static size_t enc_hash(noise_hs *s, const uint8_t *pt, size_t n, uint8_t *out){
    if(!s->has_k){ memcpy(out,pt,n); mix_hash(s,out,n); return n; }
    { uint8_t nn[12]; nonce12(nn,s->n++); aead_seal(out,s->k,nn,s->h,32,pt,n); mix_hash(s,out,n+16); return n+16; }
}
static int dec_hash(noise_hs *s, const uint8_t *ct, size_t n, uint8_t *out){
    if(!s->has_k){ memcpy(out,ct,n); mix_hash(s,ct,n); return (int)n; }
    { uint8_t nn[12]; if(n<16) return -1; nonce12(nn,s->n); if(aead_open(out,s->k,nn,s->h,32,ct,n)) return -1; s->n++; mix_hash(s,ct,n); return (int)n-16; }
}
static void dh(uint8_t out[32], const uint8_t sk[32], const uint8_t pk[32]){crypto_x25519(out,sk,pk);}
void noise_init(noise_hs *s, int initiator, const uint8_t s_sk[32]){
    memset(s,0,sizeof *s);
    blake2s(s->h, PROTO, strlen(PROTO)); /* 33 bytes > HASHLEN */
    memcpy(s->ck, s->h, 32);
    mix_hash(s, (const uint8_t*)"", 0); /* empty prologue */
    memcpy(s->s_sk, s_sk, 32); crypto_x25519_public_key(s->s_pk, s_sk);
    s->initiator=initiator;
}
static void gen_e(noise_hs *s){rand_bytes(s->e_sk,32); crypto_x25519_public_key(s->e_pk,s->e_sk);}
int noise_write(noise_hs *s, uint8_t *out){
    uint8_t d[32]; size_t o=0;
    if(s->step==0 && s->initiator){ gen_e(s); memcpy(out,s->e_pk,32); mix_hash(s,s->e_pk,32); o=32; o+=enc_hash(s,NULL,0,out+o); }
    else if(s->step==1 && !s->initiator){
        gen_e(s); memcpy(out,s->e_pk,32); mix_hash(s,s->e_pk,32); o=32;
        dh(d,s->e_sk,s->re); mix_key(s,d);
        o+=enc_hash(s,s->s_pk,32,out+o);
        dh(d,s->s_sk,s->re); mix_key(s,d);
        o+=enc_hash(s,NULL,0,out+o);
    } else if(s->step==2 && s->initiator){
        o+=enc_hash(s,s->s_pk,32,out+o);
        dh(d,s->s_sk,s->re); mix_key(s,d);
        o+=enc_hash(s,NULL,0,out+o);
    } else return -1;
    crypto_wipe(d,32); s->step++; return (int)o;
}
int noise_read(noise_hs *s, const uint8_t *in, size_t n){
    uint8_t d[32], tmp[64];
    if(s->step==0 && !s->initiator){
        if(n<32) return -1; memcpy(s->re,in,32); mix_hash(s,s->re,32);
        if(dec_hash(s,in+32,n-32,tmp)<0) return -1;
    } else if(s->step==1 && s->initiator){
        if(n!=32+48+16) return -1;
        memcpy(s->re,in,32); mix_hash(s,s->re,32);
        dh(d,s->e_sk,s->re); mix_key(s,d);
        if(dec_hash(s,in+32,48,s->rs)!=32) return -1;
        dh(d,s->e_sk,s->rs); mix_key(s,d);
        if(dec_hash(s,in+80,16,tmp)!=0) return -1;
    } else if(s->step==2 && !s->initiator){
        if(n!=48+16) return -1;
        if(dec_hash(s,in,48,s->rs)!=32) return -1;
        dh(d,s->e_sk,s->rs); mix_key(s,d);
        if(dec_hash(s,in+48,16,tmp)!=0) return -1;
    } else return -1;
    crypto_wipe(d,32); s->step++; return 0;
}
void noise_split(noise_hs *s, uint8_t tx[32], uint8_t rx[32]){
    uint8_t k1[32],k2[32];
    hkdf2(s->ck, NULL, 0, k1, k2);
    if(s->initiator){memcpy(tx,k1,32);memcpy(rx,k2,32);} else {memcpy(tx,k2,32);memcpy(rx,k1,32);}
    crypto_wipe(k1,32); crypto_wipe(k2,32); crypto_wipe(s->e_sk,32); crypto_wipe(s->k,32);
}
void noise_enc(const uint8_t k[32], uint64_t n, const uint8_t *pt, size_t len, uint8_t *out){uint8_t nn[12]; nonce12(nn,n); aead_seal(out,k,nn,NULL,0,pt,len);}
int noise_dec(const uint8_t k[32], uint64_t n, const uint8_t *ct, size_t len, uint8_t *out){uint8_t nn[12]; nonce12(nn,n); return aead_open(out,k,nn,NULL,0,ct,len);}

uint8_t *seal_rand(const uint8_t key[32], const uint8_t *pt, size_t n, size_t *outn){
    uint8_t *o=(uint8_t*)malloc(n+28); if(!o) return NULL;
    rand_bytes(o,12); aead_seal(o+12,key,o,NULL,0,pt,n); *outn=n+28; return o;
}
uint8_t *open_rand(const uint8_t key[32], const uint8_t *in, size_t n, size_t *outn){
    uint8_t *o; if(n<28) return NULL; o=(uint8_t*)malloc(n-28+1); if(!o) return NULL;
    if(aead_open(o,key,in,NULL,0,in+12,n-12)){free(o);return NULL;} o[n-28]=0; *outn=n-28; return o;
}
