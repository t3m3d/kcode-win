#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#if defined(_WIN32) || defined(_WIN64)
#include <string.h>
#else
#include <strings.h>
#define _stricmp strcasecmp
#endif

static int _argc; static char** _argv;

typedef struct ABlock { struct ABlock* next; int cap; int used; } ABlock;
static ABlock* _arena = 0;
static char* _alloc(int n) {
    n = (n + 7) & ~7;
    if (!_arena || _arena->used + n > _arena->cap) {
        int cap = 64*1024*1024;
        if (n > cap) cap = n;
        ABlock* b = (ABlock*)malloc(sizeof(ABlock) + cap);
        if (!b) { fprintf(stderr, "out of memory\n"); exit(1); }
        b->cap = cap; b->used = 0; b->next = _arena; _arena = b;
    }
    char* p = (char*)(_arena + 1) + _arena->used;
    _arena->used += n;
    return p;
}

static char _K_EMPTY[] = "";
static char _K_ZERO[] = "0";
static char _K_ONE[] = "1";

static char* kr_str(const char* s) {
    if (!s[0]) return _K_EMPTY;
    if (s[0] == '0' && !s[1]) return _K_ZERO;
    if (s[0] == '1' && !s[1]) return _K_ONE;
    int n = (int)strlen(s) + 1;
    char* p = _alloc(n);
    memcpy(p, s, n);
    return p;
}

static char* kr_cat(const char* a, const char* b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    char* p = _alloc(la + lb + 1);
    memcpy(p, a, la);
    memcpy(p + la, b, lb + 1);
    return p;
}

static int kr_isnum(const char* s) {
    if (!*s) return 0;
    const char* p = s;
    if (*p == '-') p++;
    if (!*p) return 0;
    while (*p) { if (*p < '0' || *p > '9') return 0; p++; }
    return 1;
}

static char* kr_itoa(int v) {
    if (v == 0) return _K_ZERO;
    if (v == 1) return _K_ONE;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", v);
    return kr_str(buf);
}

static int kr_atoi(const char* s) { return atoi(s); }

static char* kr_plus(const char* a, const char* b) {
    if (kr_isnum(a) && kr_isnum(b))
        return kr_itoa(atoi(a) + atoi(b));
    return kr_cat(a, b);
}

static char* kr_sub(const char* a, const char* b) { return kr_itoa(atoi(a) - atoi(b)); }
static char* kr_mul(const char* a, const char* b) { return kr_itoa(atoi(a) * atoi(b)); }
static char* kr_div(const char* a, const char* b) { return kr_itoa(atoi(a) / atoi(b)); }
static char* kr_mod(const char* a, const char* b) { return kr_itoa(atoi(a) % atoi(b)); }
static char* kr_neg(const char* a) { return kr_itoa(-atoi(a)); }
static char* kr_not(const char* a) { return atoi(a) ? _K_ZERO : _K_ONE; }

static char* kr_eq(const char* a, const char* b) {
    return strcmp(a, b) == 0 ? _K_ONE : _K_ZERO;
}
static char* kr_neq(const char* a, const char* b) {
    return strcmp(a, b) != 0 ? _K_ONE : _K_ZERO;
}
static char* kr_lt(const char* a, const char* b) {
    if (kr_isnum(a) && kr_isnum(b)) return atoi(a) < atoi(b) ? _K_ONE : _K_ZERO;
    return strcmp(a, b) < 0 ? _K_ONE : _K_ZERO;
}
static char* kr_gt(const char* a, const char* b) {
    if (kr_isnum(a) && kr_isnum(b)) return atoi(a) > atoi(b) ? _K_ONE : _K_ZERO;
    return strcmp(a, b) > 0 ? _K_ONE : _K_ZERO;
}
static char* kr_lte(const char* a, const char* b) {
    return kr_gt(a, b) == _K_ZERO ? _K_ONE : _K_ZERO;
}
static char* kr_gte(const char* a, const char* b) {
    return kr_lt(a, b) == _K_ZERO ? _K_ONE : _K_ZERO;
}

static int kr_truthy(const char* s) {
    if (!s || !*s) return 0;
    if (strcmp(s, "0") == 0) return 0;
    if (strcmp(s, "false") == 0) return 0;
    return 1;
}

static char* kr_print(const char* s) {
    printf("%s\n", s);
    return _K_EMPTY;
}

static char* kr_len(const char* s) { return kr_itoa((int)strlen(s)); }

static char* kr_idx(const char* s, int i) {
    char buf[2] = {s[i], 0};
    return kr_str(buf);
}

static char* kr_split(const char* s, const char* idxs) {
    int idx = atoi(idxs);
    int count = 0;
    const char* start = s;
    const char* p = s;
    while (*p) {
        if (*p == ',') {
            if (count == idx) {
                int len = (int)(p - start);
                char* r = _alloc(len + 1);
                memcpy(r, start, len);
                r[len] = 0;
                return r;
            }
            count++;
            start = p + 1;
        }
        p++;
    }
    if (count == idx) return kr_str(start);
    return kr_str("");
}

static char* kr_startswith(const char* s, const char* prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0 ? _K_ONE : _K_ZERO;
}

static char* kr_substr(const char* s, const char* starts, const char* ends) {
    int st = atoi(starts), en = atoi(ends);
    int slen = (int)strlen(s);
    if (st >= slen) return kr_str("");
    if (en > slen) en = slen;
    int n = en - st;
    if (n <= 0) return kr_str("");
    char* r = _alloc(n + 1);
    memcpy(r, s + st, n);
    r[n] = 0;
    return r;
}

static char* kr_toint(const char* s) { return kr_itoa(atoi(s)); }

static char* kr_exec(const char* cmd) {
    char* buf=_alloc(8192); buf[0]=0;
#ifdef _WIN32
    FILE* p=_popen(cmd,"r");
#else
    FILE* p=popen(cmd,"r");
#endif
    if(!p) return buf;
    int pos=0,ch;
    while(pos<8191&&(ch=fgetc(p))!=EOF) buf[pos++]=(char)ch;
    buf[pos]=0;
#ifdef _WIN32
    _pclose(p);
#else
    pclose(p);
#endif
    while(pos>0&&(buf[pos-1]==13||buf[pos-1]==10||buf[pos-1]==32)) buf[--pos]=0;
    return buf;
}



static char* kr_readfile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return kr_str("");
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = _alloc((int)sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = 0;
    fclose(f);
    return buf;
}

static char* kr_arg(const char* idxs) {
    int idx = atoi(idxs) + 1;
    if (idx < _argc) return kr_str(_argv[idx]);
    return kr_str("");
}

static char* kr_argcount() {
    return kr_itoa(_argc - 1);
}

static char* kr_getline(const char* s, const char* idxs) {
    int idx = atoi(idxs);
    static const char* _gl_s = 0;
    static int _gl_idx = 0;
    static const char* _gl_start = 0;
    const char* start; int cur;
    if (s == _gl_s && _gl_start && idx >= _gl_idx) {
        start = _gl_start; cur = _gl_idx;
    } else {
        start = s; cur = 0; _gl_s = s; _gl_start = s; _gl_idx = 0;
    }
    const char* p = start;
    while (*p) {
        if (*p == '\n') {
            if (cur == idx) {
                int len = (int)(p - start);
                char* r = _alloc(len + 1);
                memcpy(r, start, len); r[len] = 0;
                _gl_s = s; _gl_idx = idx; _gl_start = start;
                return r;
            }
            cur++; start = p + 1;
        }
        p++;
    }
    if (cur == idx) { _gl_s = s; _gl_idx = idx; _gl_start = start; return kr_str(start); }
    return kr_str("");
}

static char* kr_linecount(const char* s) {
    if (!*s) return kr_str("0");
    int count = 1;
    const char* p = s;
    while (*p) { if (*p == '\n') count++; p++; }
    if (*(p - 1) == '\n') count--;
    return kr_itoa(count);
}

static char* kr_count(const char* s) {
    int n = 1;
    if (s) { const char* p = s; while (*p) { if (*p == ',') n++; p++; } }
    return kr_itoa(n);
}

static char* kr_writefile(const char* path, const char* data) {
    FILE* f = fopen(path, "wb");
    if (!f) return _K_ZERO;
    fwrite(data, 1, strlen(data), f);
    fclose(f);
    return _K_ONE;
}

static int _krhex(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static char* kr_writebytes(const char* path, const char* hexstr) {
    FILE* f = fopen(path, "wb");
    if (!f) return _K_ZERO;
    const char* p = hexstr;
    while (*p) {
        if (*p == 'x' && p[1] && p[2]) {
            int hi = _krhex(p[1]), lo = _krhex(p[2]);
            if (hi >= 0 && lo >= 0) { unsigned char b = (unsigned char)(hi*16+lo); fwrite(&b,1,1,f); }
            p += 3;
        } else { p++; }
    }
    fclose(f);
    return _K_ONE;
}

static char* kr_shellrun(const char* cmd){int r=system(cmd);return kr_itoa(r);}
static char* kr_deletefile(const char* path){remove(path);return _K_EMPTY;}
static char* exec(const char* cmd){return kr_exec(cmd);}
static char* shellRun(const char* cmd){return kr_shellrun(cmd);}
static char* deleteFile(const char* path){return kr_deletefile(path);}

static char* kr_input() {
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return _K_EMPTY;
    int len = (int)strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[--len] = 0;
    if (len > 0 && buf[len-1] == '\r') buf[--len] = 0;
    return kr_str(buf);
}

static char* kr_indexof(const char* s, const char* sub) {
    const char* p = strstr(s, sub);
    if (!p) return kr_itoa(-1);
    return kr_itoa((int)(p - s));
}

static char* kr_replace(const char* s, const char* old, const char* rep) {
    int slen = (int)strlen(s), olen = (int)strlen(old), rlen = (int)strlen(rep);
    if (olen == 0) return kr_str(s);
    int count = 0;
    const char* p = s;
    while ((p = strstr(p, old)) != 0) { count++; p += olen; }
    int nlen = slen + count * (rlen - olen);
    char* out = _alloc(nlen + 1);
    char* dst = out;
    p = s;
    while (*p) {
        if (strncmp(p, old, olen) == 0) {
            memcpy(dst, rep, rlen); dst += rlen; p += olen;
        } else { *dst++ = *p++; }
    }
    *dst = 0;
    return out;
}

static char* kr_charat(const char* s, const char* idxs) {
    int i = atoi(idxs);
    int slen = (int)strlen(s);
    if (i < 0 || i >= slen) return _K_EMPTY;
    char buf[2] = {s[i], 0};
    return kr_str(buf);
}

static char* kr_trim(const char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1]==' '||s[len-1]=='\t'||s[len-1]=='\n'||s[len-1]=='\r')) len--;
    char* r = _alloc(len + 1);
    memcpy(r, s, len);
    r[len] = 0;
    return r;
}

static char* kr_tolower(const char* s) {
    int len = (int)strlen(s);
    char* out = _alloc(len + 1);
    for (int i = 0; i <= len; i++)
        out[i] = (s[i] >= 'A' && s[i] <= 'Z') ? s[i] + 32 : s[i];
    return out;
}

static char* kr_toupper(const char* s) {
    int len = (int)strlen(s);
    char* out = _alloc(len + 1);
    for (int i = 0; i <= len; i++)
        out[i] = (s[i] >= 'a' && s[i] <= 'z') ? s[i] - 32 : s[i];
    return out;
}

static char* kr_contains(const char* s, const char* sub) {
    return strstr(s, sub) ? _K_ONE : _K_ZERO;
}

static char* kr_endswith(const char* s, const char* suffix) {
    int slen = (int)strlen(s), suflen = (int)strlen(suffix);
    if (suflen > slen) return _K_ZERO;
    return strcmp(s + slen - suflen, suffix) == 0 ? _K_ONE : _K_ZERO;
}

static char* kr_abs(const char* a) { int v = atoi(a); return kr_itoa(v < 0 ? -v : v); }
static char* kr_min(const char* a, const char* b) { return atoi(a) <= atoi(b) ? kr_str(a) : kr_str(b); }
static char* kr_max(const char* a, const char* b) { return atoi(a) >= atoi(b) ? kr_str(a) : kr_str(b); }

static char* kr_exit(const char* code) { exit(atoi(code)); return _K_EMPTY; }

static char* kr_type(const char* s) {
    if (kr_isnum(s)) return kr_str("number");
    return kr_str("string");
}

static char* kr_append(const char* lst, const char* item) {
    if (!*lst) return kr_str(item);
    return kr_cat(kr_cat(lst, ","), item);
}

static char* kr_join(const char* lst, const char* sep) {
    int llen = (int)strlen(lst), slen = (int)strlen(sep);
    int rlen = 0;
    for (int i = 0; i < llen; i++) {
        if (lst[i] == ',') rlen += slen; else rlen++;
    }
    char* out = _alloc(rlen + 1);
    int j = 0;
    for (int i = 0; i < llen; i++) {
        if (lst[i] == ',') { memcpy(out+j, sep, slen); j += slen; }
        else { out[j++] = lst[i]; }
    }
    out[j] = 0;
    return out;
}

static char* kr_reverse(const char* lst) {
    int cnt = 0;
    const char* p = lst;
    while (*p) { if (*p == ',') cnt++; p++; }
    cnt++;
    char* out = _K_EMPTY;
    for (int i = cnt - 1; i >= 0; i--) {
        char* item = kr_split(lst, kr_itoa(i));
        if (i == cnt - 1) out = item;
        else out = kr_cat(kr_cat(out, ","), item);
    }
    return out;
}

static int _kr_cmp(const void* a, const void* b) {
    const char* sa = *(const char**)a;
    const char* sb = *(const char**)b;
    if (kr_isnum(sa) && kr_isnum(sb)) return atoi(sa) - atoi(sb);
    return strcmp(sa, sb);
}
static char* kr_sort(const char* lst) {
    if (!*lst) return _K_EMPTY;
    int cnt = 1;
    const char* p = lst;
    while (*p) { if (*p == ',') cnt++; p++; }
    char** arr = (char**)_alloc(cnt * sizeof(char*));
    for (int i = 0; i < cnt; i++) arr[i] = kr_split(lst, kr_itoa(i));
    qsort(arr, cnt, sizeof(char*), _kr_cmp);
    char* out = arr[0];
    for (int i = 1; i < cnt; i++) out = kr_cat(kr_cat(out, ","), arr[i]);
    return out;
}

static char* kr_keys(const char* map) {
    if (!*map) return _K_EMPTY;
    int cnt = 1;
    const char* p = map;
    while (*p) { if (*p == ',') cnt++; p++; }
    char* out = _K_EMPTY; int first = 1;
    for (int i = 0; i < cnt; i += 2) {
        char* k = kr_split(map, kr_itoa(i));
        if (first) { out = k; first = 0; }
        else out = kr_cat(kr_cat(out, ","), k);
    }
    return out;
}

static char* kr_values(const char* map) {
    if (!*map) return _K_EMPTY;
    int cnt = 1;
    const char* p = map;
    while (*p) { if (*p == ',') cnt++; p++; }
    char* out = _K_EMPTY; int first = 1;
    for (int i = 1; i < cnt; i += 2) {
        char* v = kr_split(map, kr_itoa(i));
        if (first) { out = v; first = 0; }
        else out = kr_cat(kr_cat(out, ","), v);
    }
    return out;
}

static char* kr_haskey(const char* map, const char* key) {
    if (!*map) return _K_ZERO;
    int cnt = 1;
    const char* p = map;
    while (*p) { if (*p == ',') cnt++; p++; }
    for (int i = 0; i < cnt; i += 2) {
        if (strcmp(kr_split(map, kr_itoa(i)), key) == 0) return _K_ONE;
    }
    return _K_ZERO;
}

static char* kr_remove(const char* lst, const char* item) {
    if (!*lst) return _K_EMPTY;
    int cnt = 1;
    const char* p = lst;
    while (*p) { if (*p == ',') cnt++; p++; }
    char* out = _K_EMPTY; int first = 1;
    for (int i = 0; i < cnt; i++) {
        char* el = kr_split(lst, kr_itoa(i));
        if (strcmp(el, item) != 0) {
            if (first) { out = el; first = 0; }
            else out = kr_cat(kr_cat(out, ","), el);
        }
    }
    return out;
}

static char* kr_repeat(const char* s, const char* ns) {
    int n = atoi(ns);
    if (n <= 0) return _K_EMPTY;
    int slen = (int)strlen(s);
    char* out = _alloc(slen * n + 1);
    for (int i = 0; i < n; i++) memcpy(out + i * slen, s, slen);
    out[slen * n] = 0;
    return out;
}

static char* kr_format(const char* fmt, const char* arg) {
    char buf[4096];
    const char* p = strstr(fmt, "{}");
    if (!p) return kr_str(fmt);
    int pre = (int)(p - fmt);
    int alen = (int)strlen(arg);
    int postlen = (int)strlen(p + 2);
    if (pre + alen + postlen >= 4096) return kr_str(fmt);
    memcpy(buf, fmt, pre);
    memcpy(buf + pre, arg, alen);
    memcpy(buf + pre + alen, p + 2, postlen + 1);
    return kr_str(buf);
}

static char* kr_parseint(const char* s) {
    const char* p = s;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return _K_ZERO;
    return kr_itoa(atoi(p));
}

static char* kr_tostr(const char* s) { return kr_str(s); }

static int kr_listlen(const char* s) {
    if (!*s) return 0;
    int cnt = 1;
    while (*s) { if (*s == ',') cnt++; s++; }
    return cnt;
}

static char* kr_range(const char* starts, const char* ends) {
    int s = atoi(starts), e = atoi(ends);
    if (s >= e) return _K_EMPTY;
    char* out = kr_itoa(s);
    for (int i = s + 1; i < e; i++) out = kr_cat(kr_cat(out, ","), kr_itoa(i));
    return out;
}

static char* kr_pow(const char* bs, const char* es) {
    int b = atoi(bs), e = atoi(es), r = 1;
    for (int i = 0; i < e; i++) r *= b;
    return kr_itoa(r);
}

static char* kr_sqrt(const char* s) {
    int v = atoi(s);
    if (v <= 0) return _K_ZERO;
    int r = 0;
    while ((r + 1) * (r + 1) <= v) r++;
    return kr_itoa(r);
}

static char* kr_sign(const char* s) {
    int v = atoi(s);
    if (v > 0) return _K_ONE;
    if (v < 0) return kr_str("-1");
    return _K_ZERO;
}

static char* kr_clamp(const char* vs, const char* los, const char* his) {
    int v = atoi(vs), lo = atoi(los), hi = atoi(his);
    if (v < lo) return kr_str(los);
    if (v > hi) return kr_str(his);
    return kr_str(vs);
}

static char* kr_padleft(const char* s, const char* ws, const char* pad) {
    int w = atoi(ws), slen = (int)strlen(s), plen = (int)strlen(pad);
    if (slen >= w || plen == 0) return kr_str(s);
    int need = w - slen;
    char* out = _alloc(w + 1);
    for (int i = 0; i < need; i++) out[i] = pad[i % plen];
    memcpy(out + need, s, slen + 1);
    return out;
}

static char* kr_padright(const char* s, const char* ws, const char* pad) {
    int w = atoi(ws), slen = (int)strlen(s), plen = (int)strlen(pad);
    if (slen >= w || plen == 0) return kr_str(s);
    int need = w - slen;
    char* out = _alloc(w + 1);
    memcpy(out, s, slen);
    for (int i = 0; i < need; i++) out[slen + i] = pad[i % plen];
    out[w] = 0;
    return out;
}

static char* kr_charcode(const char* s) {
    if (!*s) return _K_ZERO;
    return kr_itoa((unsigned char)s[0]);
}

static char* kr_fromcharcode(const char* ns) {
    char buf[2] = {(char)atoi(ns), 0};
    return kr_str(buf);
}

static char* kr_slice(const char* lst, const char* starts, const char* ends) {
    int cnt = kr_listlen(lst);
    int s = atoi(starts), e = atoi(ends);
    if (s < 0) s = cnt + s;
    if (e < 0) e = cnt + e;
    if (s < 0) s = 0;
    if (e > cnt) e = cnt;
    if (s >= e) return _K_EMPTY;
    char* out = kr_split(lst, kr_itoa(s));
    for (int i = s + 1; i < e; i++)
        out = kr_cat(kr_cat(out, ","), kr_split(lst, kr_itoa(i)));
    return out;
}

static char* kr_length(const char* lst) {
    return kr_itoa(kr_listlen(lst));
}

static char* kr_unique(const char* lst) {
    if (!*lst) return _K_EMPTY;
    int cnt = kr_listlen(lst);
    char* out = _K_EMPTY; int oc = 0;
    for (int i = 0; i < cnt; i++) {
        char* item = kr_split(lst, kr_itoa(i));
        int dup = 0;
        for (int j = 0; j < oc; j++) {
            if (strcmp(kr_split(out, kr_itoa(j)), item) == 0) { dup = 1; break; }
        }
        if (!dup) {
            if (oc == 0) out = item; else out = kr_cat(kr_cat(out, ","), item);
            oc++;
        }
    }
    return out;
}

static char* kr_printerr(const char* s) {
    fprintf(stderr, "%s\n", s);
    return _K_EMPTY;
}

static char* kr_readline(const char* prompt) {
    if (*prompt) printf("%s", prompt);
    fflush(stdout);
    char buf[4096];
    if (!fgets(buf, sizeof(buf), stdin)) return _K_EMPTY;
    int len = (int)strlen(buf);
    if (len > 0 && buf[len-1] == '\n') buf[--len] = 0;
    if (len > 0 && buf[len-1] == '\r') buf[--len] = 0;
    return kr_str(buf);
}

static char* kr_assert(const char* cond, const char* msg) {
    if (!kr_truthy(cond)) {
        fprintf(stderr, "ASSERTION FAILED: %s\n", msg);
        exit(1);
    }
    return _K_ONE;
}

static char* kr_splitby(const char* s, const char* delim) {
    int slen = (int)strlen(s), dlen = (int)strlen(delim);
    if (dlen == 0 || slen == 0) return kr_str(s);
    char* out = _K_EMPTY; int first = 1;
    const char* p = s;
    while (*p) {
        const char* f = strstr(p, delim);
        if (!f) { 
            if (first) out = kr_str(p); else out = kr_cat(kr_cat(out, ","), kr_str(p));
            break;
        }
        int n = (int)(f - p);
        char* chunk = _alloc(n + 1);
        memcpy(chunk, p, n); chunk[n] = 0;
        if (first) { out = chunk; first = 0; }
        else out = kr_cat(kr_cat(out, ","), chunk);
        p = f + dlen;
        if (!*p) { out = kr_cat(kr_cat(out, ","), _K_EMPTY); break; }
    }
    return out;
}

static char* kr_listindexof(const char* lst, const char* item) {
    if (!*lst) return kr_itoa(-1);
    int cnt = kr_listlen(lst);
    for (int i = 0; i < cnt; i++) {
        if (strcmp(kr_split(lst, kr_itoa(i)), item) == 0) return kr_itoa(i);
    }
    return kr_itoa(-1);
}

static char* kr_insertat(const char* lst, const char* idxs, const char* item) {
    int idx = atoi(idxs);
    int cnt = kr_listlen(lst);
    if (!*lst && idx == 0) return kr_str(item);
    if (idx < 0) idx = 0;
    if (idx >= cnt) return kr_cat(kr_cat(lst, ","), item);
    char* out = _K_EMPTY; int first = 1;
    for (int i = 0; i < cnt; i++) {
        if (i == idx) {
            if (first) { out = kr_str(item); first = 0; }
            else out = kr_cat(kr_cat(out, ","), item);
        }
        char* el = kr_split(lst, kr_itoa(i));
        if (first) { out = el; first = 0; }
        else out = kr_cat(kr_cat(out, ","), el);
    }
    return out;
}

static char* kr_removeat(const char* lst, const char* idxs) {
    int idx = atoi(idxs);
    int cnt = kr_listlen(lst);
    if (idx < 0 || idx >= cnt) return kr_str(lst);
    char* out = _K_EMPTY; int first = 1;
    for (int i = 0; i < cnt; i++) {
        if (i == idx) continue;
        char* el = kr_split(lst, kr_itoa(i));
        if (first) { out = el; first = 0; }
        else out = kr_cat(kr_cat(out, ","), el);
    }
    return out;
}

static char* kr_replaceat(const char* lst, const char* idxs, const char* val) {
    int idx = atoi(idxs);
    int cnt = kr_listlen(lst);
    if (idx < 0 || idx >= cnt) return kr_str(lst);
    char* out = _K_EMPTY; int first = 1;
    for (int i = 0; i < cnt; i++) {
        char* el = (i == idx) ? (char*)val : kr_split(lst, kr_itoa(i));
        if (first) { out = el; first = 0; }
        else out = kr_cat(kr_cat(out, ","), el);
    }
    return out;
}

static char* kr_fill(const char* ns, const char* val) {
    int n = atoi(ns);
    if (n <= 0) return _K_EMPTY;
    char* out = kr_str(val);
    for (int i = 1; i < n; i++) out = kr_cat(kr_cat(out, ","), val);
    return out;
}

static char* kr_zip(const char* a, const char* b) {
    int ac = kr_listlen(a), bc = kr_listlen(b);
    int mc = ac < bc ? ac : bc;
    if (!*a || !*b) return _K_EMPTY;
    char* out = _K_EMPTY; int first = 1;
    for (int i = 0; i < mc; i++) {
        char* ai = kr_split(a, kr_itoa(i));
        char* bi = kr_split(b, kr_itoa(i));
        if (first) { out = kr_cat(kr_cat(ai, ","), bi); first = 0; }
        else { out = kr_cat(kr_cat(out, ","), kr_cat(kr_cat(ai, ","), bi)); }
    }
    return out;
}

static char* kr_every(const char* lst, const char* val) {
    if (!*lst) return _K_ONE;
    int cnt = kr_listlen(lst);
    for (int i = 0; i < cnt; i++) {
        if (strcmp(kr_split(lst, kr_itoa(i)), val) != 0) return _K_ZERO;
    }
    return _K_ONE;
}

static char* kr_some(const char* lst, const char* val) {
    if (!*lst) return _K_ZERO;
    int cnt = kr_listlen(lst);
    for (int i = 0; i < cnt; i++) {
        if (strcmp(kr_split(lst, kr_itoa(i)), val) == 0) return _K_ONE;
    }
    return _K_ZERO;
}

static char* kr_countof(const char* lst, const char* item) {
    if (!*lst) return _K_ZERO;
    int cnt = kr_listlen(lst), c = 0;
    for (int i = 0; i < cnt; i++) {
        if (strcmp(kr_split(lst, kr_itoa(i)), item) == 0) c++;
    }
    return kr_itoa(c);
}

static char* kr_sumlist(const char* lst) {
    if (!*lst) return _K_ZERO;
    int cnt = kr_listlen(lst), s = 0;
    for (int i = 0; i < cnt; i++) s += atoi(kr_split(lst, kr_itoa(i)));
    return kr_itoa(s);
}

static char* kr_maxlist(const char* lst) {
    if (!*lst) return _K_ZERO;
    int cnt = kr_listlen(lst);
    int m = atoi(kr_split(lst, _K_ZERO));
    for (int i = 1; i < cnt; i++) {
        int v = atoi(kr_split(lst, kr_itoa(i)));
        if (v > m) m = v;
    }
    return kr_itoa(m);
}

static char* kr_minlist(const char* lst) {
    if (!*lst) return _K_ZERO;
    int cnt = kr_listlen(lst);
    int m = atoi(kr_split(lst, _K_ZERO));
    for (int i = 1; i < cnt; i++) {
        int v = atoi(kr_split(lst, kr_itoa(i)));
        if (v < m) m = v;
    }
    return kr_itoa(m);
}

static char* kr_hex(const char* s) {
    int v = atoi(s);
    char buf[32];
    snprintf(buf, sizeof(buf), "%x", v < 0 ? -v : v);
    if (v < 0) return kr_cat("-", kr_str(buf));
    return kr_str(buf);
}

static char* kr_bin(const char* s) {
    int v = atoi(s);
    if (v == 0) return _K_ZERO;
    int neg = v < 0; if (neg) v = -v;
    char buf[64]; int i = 63; buf[i] = 0;
    while (v > 0) { buf[--i] = '0' + (v & 1); v >>= 1; }
    if (neg) return kr_cat("-", kr_str(&buf[i]));
    return kr_str(&buf[i]);
}

typedef struct EnvEntry { char* name; char* value; struct EnvEntry* prev; } EnvEntry;

static char* kr_envnew() { return (char*)0; }

static char* kr_envset(char* envp, const char* name, const char* val) {
    EnvEntry* e = (EnvEntry*)_alloc(sizeof(EnvEntry));
    e->name = (char*)name;
    e->value = (char*)val;
    e->prev = (EnvEntry*)envp;
    return (char*)e;
}

static char* kr_envget(char* envp, const char* name) {
    EnvEntry* e = (EnvEntry*)envp;
    while (e) {
        if (strcmp(e->name, name) == 0) return e->value;
        e = e->prev;
    }
    if (strcmp(name, "__argOffset") != 0)
        fprintf(stderr, "ERROR: undefined variable: %s\n", name);
    return kr_str("");
}

typedef struct ResultStruct { char tag; char* val; char* env; int pos; } ResultStruct;

static char* kr_makeresult(const char* tag, const char* val, const char* env, const char* pos) {
    ResultStruct* r = (ResultStruct*)_alloc(sizeof(ResultStruct));
    r->tag = tag[0];
    r->val = (char*)val;
    r->env = (char*)env;
    r->pos = atoi(pos);
    return (char*)r;
}

static char* kr_getresulttag(const char* r) {
    char buf[2] = {((ResultStruct*)r)->tag, 0};
    return kr_str(buf);
}

static char* kr_getresultval(const char* r) {
    return ((ResultStruct*)r)->val;
}

static char* kr_getresultenv(const char* r) {
    return ((ResultStruct*)r)->env;
}

static char* kr_getresultpos(const char* r) {
    return kr_itoa(((ResultStruct*)r)->pos);
}

static char* kr_istruthy(const char* s) {
    if (!s || !*s || strcmp(s, "0") == 0 || strcmp(s, "false") == 0)
        return _K_ZERO;
    return _K_ONE;
}

typedef struct { int cap; int len; } SBHdr;
#define MAX_SBS 1048576
static SBHdr* _sb_table[MAX_SBS];
static int _sb_count = 0;

static char* kr_sbnew() {
    int initcap = 65536;
    SBHdr* h = (SBHdr*)malloc(sizeof(SBHdr) + initcap);
    h->cap = initcap;
    h->len = 0;
    ((char*)(h + 1))[0] = 0;
    _sb_table[_sb_count] = h;
    return kr_itoa(_sb_count++);
}

static char* kr_sbappend(const char* handle, const char* s) {
    int idx = atoi(handle);
    SBHdr* h = _sb_table[idx];
    int slen = (int)strlen(s);
    while (h->len + slen + 1 > h->cap) {
        int newcap = h->cap * 2;
        h = (SBHdr*)realloc(h, sizeof(SBHdr) + newcap);
        h->cap = newcap;
    }
    memcpy((char*)(h + 1) + h->len, s, slen);
    h->len += slen;
    ((char*)(h + 1))[h->len] = 0;
    _sb_table[idx] = h;
    return kr_str(handle);
}

static char* kr_sbtostring(const char* handle) {
    int idx = atoi(handle);
    SBHdr* h = _sb_table[idx];
    return (char*)(h + 1);
}

#include <setjmp.h>
#define _KR_TRY_MAX 256
static jmp_buf _kr_try_stack[_KR_TRY_MAX];
static char*   _kr_err_stack[_KR_TRY_MAX];
static int     _kr_try_depth = 0;

static jmp_buf* _kr_pushtry() {
    _kr_err_stack[_kr_try_depth] = _K_EMPTY;
    return &_kr_try_stack[_kr_try_depth++];
}

static char* _kr_poptry() {
    if (_kr_try_depth > 0) _kr_try_depth--;
    return _kr_err_stack[_kr_try_depth];
}

static char* _kr_throw(const char* msg) {
    if (_kr_try_depth > 0) {
        _kr_err_stack[_kr_try_depth - 1] = (char*)msg;
        longjmp(_kr_try_stack[_kr_try_depth - 1], 1);
    }
    fprintf(stderr, "Uncaught exception: %s\n", msg);
    exit(1);
    return _K_EMPTY;
}

static char* kr_strreverse(const char* s) {
    int n = (int)strlen(s);
    char* out = _alloc(n + 1);
    for (int i = 0; i < n; i++) out[i] = s[n - 1 - i];
    out[n] = 0;
    return out;
}

static char* kr_words(const char* s) {
    if (!*s) return _K_EMPTY;
    char* out = _K_EMPTY; int first = 1;
    const char* p = s;
    while (*p == ' ' || *p == '\t') p++;
    const char* start = p;
    while (1) {
        if (*p == ' ' || *p == '\t' || *p == 0) {
            if (p > start) {
                int n = (int)(p - start);
                char* w = _alloc(n + 1);
                memcpy(w, start, n); w[n] = 0;
                if (first) { out = w; first = 0; }
                else out = kr_cat(kr_cat(out, ","), w);
            }
            if (!*p) break;
            while (*p == ' ' || *p == '\t') p++;
            start = p;
        } else { p++; }
    }
    return out;
}

static char* kr_lines(const char* s) {
    if (!*s) return _K_EMPTY;
    char* out = _K_EMPTY; int first = 1;
    const char* p = s, *start = s;
    while (1) {
        if (*p == '\n' || *p == 0) {
            int n = (int)(p - start);
            if (n > 0 && start[n-1] == '\r') n--;
            char* ln = _alloc(n + 1);
            memcpy(ln, start, n); ln[n] = 0;
            if (first) { out = ln; first = 0; }
            else out = kr_cat(kr_cat(out, ","), ln);
            if (!*p) break;
            start = p + 1;
        }
        p++;
    }
    return out;
}

static char* kr_first(const char* lst) { return kr_split(lst, _K_ZERO); }

static char* kr_last(const char* lst) {
    int cnt = kr_listlen(lst);
    if (cnt == 0) return _K_EMPTY;
    return kr_split(lst, kr_itoa(cnt - 1));
}

static char* kr_head(const char* lst, const char* ns) {
    int n = atoi(ns), cnt = kr_listlen(lst);
    if (n <= 0 || !*lst) return _K_EMPTY;
    if (n >= cnt) return kr_str(lst);
    char* out = kr_split(lst, _K_ZERO);
    for (int i = 1; i < n; i++) out = kr_cat(kr_cat(out, ","), kr_split(lst, kr_itoa(i)));
    return out;
}

static char* kr_tail(const char* lst, const char* ns) {
    int n = atoi(ns), cnt = kr_listlen(lst);
    if (n <= 0 || !*lst) return _K_EMPTY;
    if (n >= cnt) return kr_str(lst);
    int start = cnt - n;
    char* out = kr_split(lst, kr_itoa(start));
    for (int i = start + 1; i < cnt; i++) out = kr_cat(kr_cat(out, ","), kr_split(lst, kr_itoa(i)));
    return out;
}

static char* kr_lstrip(const char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    return kr_str(s);
}

static char* kr_rstrip(const char* s) {
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1]==' '||s[len-1]=='\t'||s[len-1]=='\n'||s[len-1]=='\r')) len--;
    char* r = _alloc(len + 1);
    memcpy(r, s, len); r[len] = 0;
    return r;
}

static char* kr_center(const char* s, const char* ws, const char* pad) {
    int w = atoi(ws), slen = (int)strlen(s), plen = (int)strlen(pad);
    if (slen >= w || plen == 0) return kr_str(s);
    int total = w - slen;
    int left = total / 2, right = total - left;
    char* out = _alloc(w + 1);
    for (int i = 0; i < left; i++) out[i] = pad[i % plen];
    memcpy(out + left, s, slen);
    for (int i = 0; i < right; i++) out[left + slen + i] = pad[i % plen];
    out[w] = 0;
    return out;
}

static char* kr_isalpha(const char* s) {
    if (!*s) return _K_ZERO;
    for (const char* p = s; *p; p++) if (!isalpha((unsigned char)*p)) return _K_ZERO;
    return _K_ONE;
}

static char* kr_isdigit(const char* s) {
    if (!*s) return _K_ZERO;
    for (const char* p = s; *p; p++) if (!isdigit((unsigned char)*p)) return _K_ZERO;
    return _K_ONE;
}

static char* kr_isspace(const char* s) {
    if (!*s) return _K_ZERO;
    for (const char* p = s; *p; p++) if (!isspace((unsigned char)*p)) return _K_ZERO;
    return _K_ONE;
}

static char* kr_random(const char* ns) {
    int n = atoi(ns);
    if (n <= 0) return _K_ZERO;
    return kr_itoa(rand() % n);
}

static char* kr_timestamp() {
    return kr_itoa((int)time(NULL));
}

static char* kr_environ(const char* name) {
    const char* v = getenv(name);
    if (!v) return _K_EMPTY;
    return kr_str(v);
}

static char* kr_floor(const char* s) { return kr_itoa((int)atoi(s)); }
static char* kr_ceil(const char* s)  { return kr_itoa((int)atoi(s)); }
static char* kr_round(const char* s) { return kr_itoa((int)atoi(s)); }

static char* kr_throw(const char* msg) { return _kr_throw(msg); }

static char* kr_structnew() {
    // 2 slots for count + up to 32 fields (name+val pairs)
    char** s = (char**)_alloc(66 * sizeof(char*));
    s[0] = _K_ZERO; // field count
    return (char*)s;
}

static char* kr_setfield(char* obj, const char* name, const char* val) {
    char** s = (char**)obj;
    int cnt = atoi(s[0]);
    // search for existing field
    for (int i = 0; i < cnt; i++) {
        if (strcmp(s[1 + i*2], name) == 0) {
            s[2 + i*2] = (char*)val;
            return obj;
        }
    }
    // add new field
    s[1 + cnt*2] = (char*)name;
    s[2 + cnt*2] = (char*)val;
    s[0] = kr_itoa(cnt + 1);
    return obj;
}

static char* kr_getfield(char* obj, const char* name) {
    if (!obj) return _K_EMPTY;
    char** s = (char**)obj;
    int cnt = atoi(s[0]);
    for (int i = 0; i < cnt; i++) {
        if (strcmp(s[1 + i*2], name) == 0) return s[2 + i*2];
    }
    return _K_EMPTY;
}

static char* kr_hasfield(char* obj, const char* name) {
    if (!obj) return _K_ZERO;
    char** s = (char**)obj;
    int cnt = atoi(s[0]);
    for (int i = 0; i < cnt; i++) {
        if (strcmp(s[1 + i*2], name) == 0) return _K_ONE;
    }
    return _K_ZERO;
}

static char* kr_structfields(char* obj) {
    if (!obj) return _K_EMPTY;
    char** s = (char**)obj;
    int cnt = atoi(s[0]);
    if (cnt == 0) return _K_EMPTY;
    char* out = s[1];
    for (int i = 1; i < cnt; i++) out = kr_cat(kr_cat(out, ","), s[1 + i*2]);
    return out;
}

static char* kr_mapget(const char* map, const char* key) {
    if (!*map) return _K_EMPTY;
    int cnt = kr_listlen(map);
    for (int i = 0; i < cnt - 1; i += 2) {
        if (strcmp(kr_split(map, kr_itoa(i)), key) == 0)
            return kr_split(map, kr_itoa(i + 1));
    }
    return _K_EMPTY;
}

static char* kr_mapset(const char* map, const char* key, const char* val) {
    if (!*map) return kr_cat(kr_cat(kr_str(key), ","), val);
    int cnt = kr_listlen(map);
    char* out = _K_EMPTY; int first = 1; int found = 0;
    for (int i = 0; i < cnt - 1; i += 2) {
        char* k = kr_split(map, kr_itoa(i));
        char* v = (strcmp(k, key) == 0) ? (char*)val : kr_split(map, kr_itoa(i+1));
        if (strcmp(k, key) == 0) found = 1;
        if (first) { out = kr_cat(k, kr_cat(",", v)); first = 0; }
        else out = kr_cat(out, kr_cat(",", kr_cat(k, kr_cat(",", v))));
    }
    if (!found) {
        if (first) out = kr_cat(kr_str(key), kr_cat(",", val));
        else out = kr_cat(out, kr_cat(",", kr_cat(kr_str(key), kr_cat(",", val))));
    }
    return out;
}

static char* kr_mapdel(const char* map, const char* key) {
    if (!*map) return _K_EMPTY;
    int cnt = kr_listlen(map);
    char* out = _K_EMPTY; int first = 1;
    for (int i = 0; i < cnt - 1; i += 2) {
        char* k = kr_split(map, kr_itoa(i));
        if (strcmp(k, key) != 0) {
            char* v = kr_split(map, kr_itoa(i+1));
            if (first) { out = kr_cat(k, kr_cat(",", v)); first = 0; }
            else out = kr_cat(out, kr_cat(",", kr_cat(k, kr_cat(",", v))));
        }
    }
    return out;
}

static char* kr_sprintf(const char* fmt, ...) {
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return kr_str(buf);
}

static char* kr_strsplit(const char* s, const char* delim) {
    return kr_splitby(s, delim);
}

static char* kr_listmap(const char* lst, const char* prefix, const char* suffix) {
    if (!*lst) return _K_EMPTY;
    int cnt = kr_listlen(lst);
    char* out = _K_EMPTY; int first = 1;
    for (int i = 0; i < cnt; i++) {
        char* item = kr_split(lst, kr_itoa(i));
        char* mapped = kr_cat(kr_cat(kr_str(prefix), item), suffix);
        if (first) { out = mapped; first = 0; }
        else out = kr_cat(out, kr_cat(",", mapped));
    }
    return out;
}

static char* kr_listfilter(const char* lst, const char* val) {
    if (!*lst) return _K_EMPTY;
    int cnt = kr_listlen(lst), negate = 0;
    const char* match = val;
    if (val[0] == '!') { negate = 1; match = val + 1; }
    char* out = _K_EMPTY; int first = 1;
    for (int i = 0; i < cnt; i++) {
        char* item = kr_split(lst, kr_itoa(i));
        int eq = (strcmp(item, match) == 0);
        int keep = negate ? !eq : eq;
        if (keep) {
            if (first) { out = item; first = 0; }
            else out = kr_cat(out, kr_cat(",", item));
        }
    }
    return out;
}

#include <math.h>
static char* kr_tofloat(const char* s) {
    return s;
}

static char* kr_fadd(const char* a,const char* b){char buf[64];snprintf(buf,64,"%g",atof(a)+atof(b));return kr_str(buf);}
static char* kr_fsub(const char* a,const char* b){char buf[64];snprintf(buf,64,"%g",atof(a)-atof(b));return kr_str(buf);}
static char* kr_fmul(const char* a,const char* b){char buf[64];snprintf(buf,64,"%g",atof(a)*atof(b));return kr_str(buf);}
static char* kr_fdiv(const char* a,const char* b){char buf[64];if(atof(b)==0.0)return kr_str("0");snprintf(buf,64,"%g",atof(a)/atof(b));return kr_str(buf);}
static char* kr_flt(const char* a,const char* b){return atof(a)<atof(b)?_K_ONE:_K_ZERO;}
static char* kr_fgt(const char* a,const char* b){return atof(a)>atof(b)?_K_ONE:_K_ZERO;}
static char* kr_feq(const char* a,const char* b){return atof(a)==atof(b)?_K_ONE:_K_ZERO;}
static char* kr_fsqrt(const char* a) {
    char buf[64]; snprintf(buf,64,"%g",sqrt(atof(a)));
    return kr_str(buf);
}

static char* kr_ffloor(const char* a) {
    char buf[64]; snprintf(buf,64,"%.0f",floor(atof(a)));
    return kr_str(buf);
}

static char* kr_fceil(const char* a) {
    char buf[64]; snprintf(buf,64,"%.0f",ceil(atof(a)));
    return kr_str(buf);
}

static char* kr_fround(const char* a) {
    char buf[64]; snprintf(buf,64,"%.0f",round(atof(a)));
    return kr_str(buf);
}

static char* kr_fformat(const char* a,const char* prec){char fmt[32],buf[64];snprintf(fmt,32,"%%.%sf",prec);snprintf(buf,64,fmt,atof(a));return kr_str(buf);}
static char* kr_bitand(const char* a,const char* b){return kr_itoa((int)((unsigned int)atoi(a)&(unsigned int)atoi(b)));}
static char* kr_bitor(const char* a,const char* b){return kr_itoa((int)((unsigned int)atoi(a)|(unsigned int)atoi(b)));}
static char* kr_bitxor(const char* a,const char* b){return kr_itoa((int)((unsigned int)atoi(a)^(unsigned int)atoi(b)));}
static char* kr_bitnot(const char* a){return kr_itoa((int)(~(unsigned int)atoi(a)));}
static char* kr_bitshl(const char* a,const char* b){return kr_itoa((int)((unsigned int)atoi(a)<<atoi(b)));}
static char* kr_bitshr(const char* a,const char* b){return kr_itoa((int)((unsigned int)atoi(a)>>atoi(b)));}
static char* kr_tolong(const char* s){char buf[32];snprintf(buf,32,"%lld",(long long)atoll(s));return kr_str(buf);}
static char* kr_div64(const char* a,const char* b){if(atoll(b)==0)return kr_str("0");char buf[32];snprintf(buf,32,"%lld",atoll(a)/atoll(b));return kr_str(buf);}
static char* kr_mod64(const char* a,const char* b){if(atoll(b)==0)return kr_str("0");char buf[32];long long r2=atoll(a)%atoll(b);snprintf(buf,32,"%lld",r2);return kr_str(buf);}
static char* kr_mul64(const char* a,const char* b){char buf[32];snprintf(buf,32,"%lld",atoll(a)*atoll(b));return kr_str(buf);}
static char* kr_add64(const char* a,const char* b){char buf[32];snprintf(buf,32,"%lld",atoll(a)+atoll(b));return kr_str(buf);}
static char* kr_eqignorecase(const char* a,const char* b){return _stricmp(a,b)==0?kr_str("1"):kr_str("0");}
static char* kr_handlevalid(const char* h){return (h!=NULL&&h!=(char*)(intptr_t)-1)?kr_str("1"):kr_str("0");}
static char* kr_bufgetdword(char* buf){unsigned int v=*(unsigned int*)buf;return kr_itoa((int)v);}
static char* kr_bufsetdword(char* buf,const char* vs){*(unsigned int*)buf=(unsigned int)atoi(vs);return _K_EMPTY;}
static char* kr_bufgetword(char* buf){return kr_itoa((int)(*(unsigned short*)buf));}
static char* kr_bufgetqword(char* buf){unsigned long long v=*(unsigned long long*)buf;char s[32];snprintf(s,32,"%llu",v);return kr_str(s);}
static char* kr_bufgetdwordat(char* buf,const char* off){unsigned int v=*(unsigned int*)(buf+atoi(off));return kr_itoa((int)v);}
static char* kr_bufgetqwordat(char* buf,const char* off){unsigned long long v=*(unsigned long long*)(buf+atoi(off));char s[32];snprintf(s,32,"%llu",v);return kr_str(s);}
static char* kr_bufsetbyte(char* buf,const char* off,const char* val){buf[atoi(off)]=(unsigned char)atoi(val);return _K_EMPTY;}
static char* kr_bufsetdwordat(char* buf,const char* off,const char* val){*(unsigned int*)(buf+atoi(off))=(unsigned int)atoll(val);return _K_EMPTY;}
static char* kr_handleget(char* buf){return *(char**)buf;}
static char* kr_handleint(char* ptr){char s[32];snprintf(s,32,"%d",(int)(intptr_t)ptr);return kr_str(s);}
static char* kr_ptrderef(char* ptr){return *(char**)ptr;}
static char* kr_ptrindex(char* ptr,const char* n){return ((char**)ptr)[atoi(n)];}
static char* kr_callptr1(char* fn,char* a0){return ((char*(*)(char*))(fn))(a0);}
static char* kr_callptr2(char* fn,char* a0,char* a1){return ((char*(*)(char*,char*))(fn))(a0,a1);}
static char* kr_callptr3(char* fn,char* a0,char* a1,char* a2){return ((char*(*)(char*,char*,char*))(fn))(a0,a1,a2);}
static char* kr_callptr4(char* fn,char* a0,char* a1,char* a2,char* a3){return ((char*(*)(char*,char*,char*,char*))(fn))(a0,a1,a2,a3);}
static char* kr_mkclosure(const char* fn,const char* env){int fl=strlen(fn),el=strlen(env);char* p=_alloc(fl+el+2);memcpy(p,fn,fl);p[fl]='|';memcpy(p+fl+1,env,el+1);return p;}
static char* kr_closure_fn(const char* c){const char* p=strchr(c,'|');if(!p)return(char*)c;int n=p-c;char* r2=_alloc(n+1);memcpy(r2,c,n);r2[n]=0;return r2;}
static char* kr_closure_env(const char* c){const char* p=strchr(c,'|');return p?(char*)(p+1):(char*)_K_EMPTY;}
// --- imported: c:/Users/brian/Documents/GitHub/krypton/headers/../stdlib/jsonrpc.k ---
char* krLspInit();
char* krLspReadFrame();
char* krLspWriteFrame(char*);
char* krLspLog(char*);

    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #ifdef _WIN32
        #include <io.h>
        #include <fcntl.h>
        #define KR_STRNICMP _strnicmp
    #else
        #include <strings.h>     // strncasecmp on POSIX
        #define KR_STRNICMP strncasecmp
    #endif

    static int g_inited = 0;

    char* krLspInit(void) {
        if (!g_inited) {
            #ifdef _WIN32
                // Defeat CRLF translation; binary mode preserves \r\n in headers
                // and clean byte counts in bodies.
                _setmode(_fileno(stdin),  _O_BINARY);
                _setmode(_fileno(stdout), _O_BINARY);
            #endif
            // POSIX stdin/stdout are already byte-clean — no setmode needed.
            // Disable buffering on stdout so frames flush immediately.
            setvbuf(stdout, NULL, _IONBF, 0);
            // Line-buffer stderr so logs interleave readably.
            setvbuf(stderr, NULL, _IOLBF, 0);
            g_inited = 1;
        }
        return (char*)"1";
    }

    // Reads one JSON-RPC frame. Returns a malloc'd null-terminated string
    // containing the body. Returns NULL-equivalent (a static "") on EOF.
    char* krLspReadFrame(void) {
        char header[256];
        int contentLen = -1;

        for (;;) {
            int hi = 0;
            // Read one CRLF-terminated header line.
            for (;;) {
                int ch = fgetc(stdin);
                if (ch == EOF) return (char*)"";
                if (ch == '\r') {
                    int nx = fgetc(stdin);
                    if (nx == '\n') break;
                    if (nx == EOF) return (char*)"";
                    if (hi < (int)sizeof(header) - 2) {
                        header[hi++] = (char)ch;
                        header[hi++] = (char)nx;
                    }
                } else if (ch == '\n') {
                    // tolerate bare LF
                    break;
                } else {
                    if (hi < (int)sizeof(header) - 1) header[hi++] = (char)ch;
                }
            }
            header[hi] = 0;

            // Empty line — end of header block.
            if (hi == 0) break;

            // Parse "Content-Length: N"
            const char* prefix = "Content-Length:";
            size_t plen = strlen(prefix);
            if (hi >= (int)plen && KR_STRNICMP(header, prefix, plen) == 0) {
                const char* p = header + plen;
                while (*p == ' ' || *p == '\t') p++;
                contentLen = atoi(p);
            }
            // Other headers (Content-Type) ignored.
        }

        if (contentLen < 0) return (char*)"";
        if (contentLen == 0) {
            char* empty = (char*)malloc(1);
            empty[0] = 0;
            return empty;
        }

        char* buf = (char*)malloc((size_t)contentLen + 1);
        if (!buf) return (char*)"";
        size_t got = 0;
        while (got < (size_t)contentLen) {
            size_t n = fread(buf + got, 1, (size_t)contentLen - got, stdin);
            if (n == 0) { free(buf); return (char*)""; }
            got += n;
        }
        buf[contentLen] = 0;
        return buf;
    }

    char* krLspWriteFrame(char* body) {
        if (!body) body = (char*)"";
        size_t n = strlen(body);
        fprintf(stdout, "Content-Length: %zu\r\n\r\n", n);
        fwrite(body, 1, n, stdout);
        fflush(stdout);
        return (char*)"1";
    }

    char* krLspLog(char* s) {
        if (s) fprintf(stderr, "[kls] %s\n", s);
        else   fprintf(stderr, "[kls] (null)\n");
        fflush(stderr);
        return (char*)"1";
    }

char* jrInit();
char* jrReadFrame();
char* jrWriteFrame(char*);
char* jrLog(char*);
// --- imported: c:/Users/brian/Documents/GitHub/krypton/headers/../stdlib/json_emit.k ---
char* jeStr(char*);
char* jeNum(char*);
char* jeBool(char*);
char* jeNull();
char* jeRaw(char*);
char* jeObj0();
char* jeObj(char*, char*);
char* jeObj2(char*, char*, char*, char*);
char* jeObj3(char*, char*, char*, char*, char*, char*);
char* jeObj4(char*, char*, char*, char*, char*, char*, char*, char*);
char* jeArr0();
char* jeArr1(char*);
char* jeArr2(char*, char*);
char* jeArrFrom(char*);
char* jePos(char*, char*);
char* jeRange(char*, char*, char*, char*);
char* jeDiagnostic(char*, char*, char*, char*, char*, char*);
// --- imported: c:/Users/brian/Documents/GitHub/krypton/headers/../stdlib/json_parse.k ---
char* sepRes();
char* sepEntry();
char* sepKV();
char* jmNew();
char* jmSet(char*, char*, char*);
char* jmGet(char*, char*);
char* jmHas(char*, char*);
char* jpParse(char*);
char* jpGet(char*, char*);
char* jpTypeOf(char*, char*);
char* jpHas(char*, char*);
char* jpArrLen(char*, char*);
char* jpObjKeys(char*, char*);
char* jpNew(char*);
char* jpHeaderField(char*, char*);
char* jpI(char*);
char* jpLine(char*);
char* jpCol(char*);
char* jpText(char*);
char* jpAdv(char*, char*);
char* jpPeek(char*);
char* jpSkipWS(char*);
char* jpRes(char*, char*);
char* jpStOf(char*);
char* jpMap(char*);
char* jpRecord(char*, char*, char*, char*);
char* jpParseValue(char*, char*, char*);
char* jpParseString(char*, char*, char*);
char* jpParseNumber(char*, char*, char*);
char* jpParseBool(char*, char*, char*);
char* jpParseNull(char*, char*, char*);
char* jpJoin(char*, char*);
char* jpParseObject(char*, char*, char*);
char* jpParseArray(char*, char*, char*);
// --- imported: backend/build.k ---
char* krBuildExec(char*, char*);

    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>

    // Sink signature: void sink(char* chunk).
    // The chunk buffer is owned by us — sink should copy if it needs
    // to retain it past the call.
    typedef void (*kr_build_sink_t)(char*);

    char* krBuildExec(char* cmd, char* sinkPtr) {
        if (!cmd) return (char*)"-1";
        kr_build_sink_t sink = (kr_build_sink_t)sinkPtr;

#ifdef _WIN32
        FILE* p = _popen(cmd, "r");
#else
        FILE* p = popen(cmd, "r");
#endif
        if (!p) return (char*)"-1";

        char buf[4096];
        for (;;) {
            size_t n = fread(buf, 1, sizeof(buf) - 1, p);
            if (n == 0) break;
            buf[n] = 0;
            if (sink) sink(buf);
        }

#ifdef _WIN32
        int code = _pclose(p);
#else
        int code = pclose(p);
#endif
        // _pclose returns the raw exit code on Windows; on POSIX it's
        // wait status — strip the lower 8 bits via WEXITSTATUS-like
        // shift. For simplicity we emit the raw value as a decimal
        // string; callers can sniff for nonzero == failed.
        static char out[32];
        snprintf(out, sizeof(out), "%d", code);
        return out;
    }

char* bExecStream(char*, char*);

char* jrInit() {
    return ((char*(*)(void))krLspInit)();
}

char* jrReadFrame() {
    return ((char*(*)(void))krLspReadFrame)();
}

char* jrWriteFrame(char* b) {
    return ((char*(*)(char*))krLspWriteFrame)(b);
}

char* jrLog(char* s) {
    return ((char*(*)(char*))krLspLog)(s);
}

char* jeStr(char* s) {
    char* sb = kr_sbnew();
    sb = kr_sbappend(sb, kr_str("\""));
    char* i = kr_str("0");
    while (kr_truthy(kr_lt(i, kr_len(s)))) {
        char* c = kr_idx(s, kr_atoi(i));
        if (kr_truthy(kr_eq(c, kr_str("\\")))) {
            sb = kr_sbappend(sb, kr_str("\\\\"));
        } else if (kr_truthy(kr_eq(c, kr_str("\"")))) {
            sb = kr_sbappend(sb, kr_str("\\\""));
        } else if (kr_truthy(kr_eq(c, kr_str("\n")))) {
            sb = kr_sbappend(sb, kr_str("\\n"));
        } else if (kr_truthy(kr_eq(c, kr_str("\\r")))) {
            sb = kr_sbappend(sb, kr_str("\\r"));
        } else if (kr_truthy(kr_eq(c, kr_str("\t")))) {
            sb = kr_sbappend(sb, kr_str("\\t"));
        } else if (kr_truthy(kr_eq(c, kr_str("\\b")))) {
            sb = kr_sbappend(sb, kr_str("\\b"));
        } else if (kr_truthy(kr_eq(c, kr_str("\\f")))) {
            sb = kr_sbappend(sb, kr_str("\\f"));
        } else if (kr_truthy(kr_lt(kr_charcode(c), kr_str("32")))) {
            char* v = kr_charcode(c);
            sb = kr_sbappend(sb, kr_str("\\u00"));
            sb = kr_sbappend(sb, kr_hex(kr_div(v, kr_str("16"))));
            sb = kr_sbappend(sb, kr_hex(kr_mod(v, kr_str("16"))));
        } else {
            sb = kr_sbappend(sb, c);
        }
        i = kr_plus(i, kr_str("1"));
    }
    sb = kr_sbappend(sb, kr_str("\""));
    return kr_sbtostring(sb);
}

char* jeNum(char* n) {
    return kr_plus(n, kr_str(""));
}

char* jeBool(char* b) {
    if (kr_truthy(kr_eq(b, kr_str("0")))) {
        return kr_str("false");
    }
    return kr_str("true");
}

char* jeNull() {
    return kr_str("null");
}

char* jeRaw(char* s) {
    return s;
}

char* jeObj0() {
    return kr_str("{}");
}

char* jeObj(char* k1, char* v1) {
    return kr_plus(kr_plus(kr_plus(kr_plus(kr_str("{"), ((char*(*)(char*))jeStr)(k1)), kr_str(":")), v1), kr_str("}"));
}

char* jeObj2(char* k1, char* v1, char* k2, char* v2) {
    return kr_plus(kr_plus(kr_plus(kr_plus(kr_plus(kr_plus(kr_plus(kr_plus(kr_str("{"), ((char*(*)(char*))jeStr)(k1)), kr_str(":")), v1), kr_str(",")), ((char*(*)(char*))jeStr)(k2)), kr_str(":")), v2), kr_str("}"));
}

char* jeObj3(char* k1, char* v1, char* k2, char* v2, char* k3, char* v3) {
    return kr_plus(kr_plus(kr_plus(kr_plus(kr_plus(kr_plus(kr_plus(kr_plus(kr_plus(kr_plus(kr_plus(kr_plus(kr_str("{"), ((char*(*)(char*))jeStr)(k1)), kr_str(":")), v1), kr_str(",")), ((char*(*)(char*))jeStr)(k2)), kr_str(":")), v2), kr_str(",")), ((char*(*)(char*))jeStr)(k3)), kr_str(":")), v3), kr_str("}"));
}

char* jeObj4(char* k1, char* v1, char* k2, char* v2, char* k3, char* v3, char* k4, char* v4) {
    char* sb = kr_sbnew();
    sb = kr_sbappend(sb, kr_str("{"));
    sb = kr_sbappend(sb, kr_plus(kr_plus(kr_plus(((char*(*)(char*))jeStr)(k1), kr_str(":")), v1), kr_str(",")));
    sb = kr_sbappend(sb, kr_plus(kr_plus(kr_plus(((char*(*)(char*))jeStr)(k2), kr_str(":")), v2), kr_str(",")));
    sb = kr_sbappend(sb, kr_plus(kr_plus(kr_plus(((char*(*)(char*))jeStr)(k3), kr_str(":")), v3), kr_str(",")));
    sb = kr_sbappend(sb, kr_plus(kr_plus(((char*(*)(char*))jeStr)(k4), kr_str(":")), v4));
    sb = kr_sbappend(sb, kr_str("}"));
    return kr_sbtostring(sb);
}

char* jeArr0() {
    return kr_str("[]");
}

char* jeArr1(char* a) {
    return kr_plus(kr_plus(kr_str("["), a), kr_str("]"));
}

char* jeArr2(char* a, char* b) {
    return kr_plus(kr_plus(kr_plus(kr_plus(kr_str("["), a), kr_str(",")), b), kr_str("]"));
}

char* jeArrFrom(char* tabList) {
    if (kr_truthy(kr_eq(tabList, kr_str("")))) {
        return kr_str("[]");
    }
    char* sb = kr_sbnew();
    sb = kr_sbappend(sb, kr_str("["));
    char* n = kr_len(tabList);
    char* i = kr_str("0");
    char* start = kr_str("0");
    char* first = kr_str("1");
    while (kr_truthy(kr_lt(i, n))) {
        if (kr_truthy(kr_eq(kr_idx(tabList, kr_atoi(i)), kr_str("\t")))) {
            if (kr_truthy(kr_eq(first, kr_str("0")))) {
                sb = kr_sbappend(sb, kr_str(","));
            }
            sb = kr_sbappend(sb, kr_substr(tabList, start, i));
            first = kr_str("0");
            start = kr_plus(i, kr_str("1"));
        }
        i = kr_plus(i, kr_str("1"));
    }
    if (kr_truthy(kr_eq(first, kr_str("0")))) {
        sb = kr_sbappend(sb, kr_str(","));
    }
    sb = kr_sbappend(sb, kr_substr(tabList, start, n));
    sb = kr_sbappend(sb, kr_str("]"));
    return kr_sbtostring(sb);
}

char* jePos(char* line, char* col) {
    return ((char*(*)(char*,char*,char*,char*))jeObj2)(kr_str("line"), ((char*(*)(char*))jeNum)(line), kr_str("character"), ((char*(*)(char*))jeNum)(col));
}

char* jeRange(char* sl, char* sc, char* el, char* ec) {
    return ((char*(*)(char*,char*,char*,char*))jeObj2)(kr_str("start"), ((char*(*)(char*,char*))jePos)(sl, sc), kr_str("end"), ((char*(*)(char*,char*))jePos)(el, ec));
}

char* jeDiagnostic(char* sl, char* sc, char* el, char* ec, char* severity, char* message) {
    return ((char*(*)(char*,char*,char*,char*,char*,char*))jeObj3)(kr_str("range"), ((char*(*)(char*,char*,char*,char*))jeRange)(sl, sc, el, ec), kr_str("severity"), ((char*(*)(char*))jeNum)(severity), kr_str("message"), ((char*(*)(char*))jeStr)(message));
}

char* sepRes() {
    return kr_fromcharcode(kr_str("2"));
}

char* sepEntry() {
    return kr_fromcharcode(kr_str("3"));
}

char* sepKV() {
    return kr_fromcharcode(kr_str("4"));
}

char* jmNew() {
    return kr_str("");
}

char* jmSet(char* m, char* k, char* v) {
    char* n = kr_len(m);
    char* i = kr_str("0");
    char* entryStart = kr_str("0");
    char* sb = kr_sbnew();
    char* replaced = kr_str("0");
    while (kr_truthy(kr_lt(i, n))) {
        if (kr_truthy(kr_eq(kr_idx(m, kr_atoi(i)), ((char*(*)(void))sepEntry)()))) {
            char* entry = kr_substr(m, entryStart, i);
            char* sep = kr_neg(kr_str("1"));
            char* j = kr_str("0");
            while (kr_truthy(kr_lt(j, kr_len(entry)))) {
                if (kr_truthy(kr_eq(kr_idx(entry, kr_atoi(j)), ((char*(*)(void))sepKV)()))) {
                    sep = j;
                    break;
                }
                j = kr_plus(j, kr_str("1"));
            }
            if (kr_truthy(kr_gte(sep, kr_str("0")))) {
                char* kk = kr_substr(entry, kr_str("0"), sep);
                if (kr_truthy(kr_eq(kk, k))) {
                    sb = kr_sbappend(sb, kr_plus(kr_plus(kr_plus(kk, ((char*(*)(void))sepKV)()), v), ((char*(*)(void))sepEntry)()));
                    replaced = kr_str("1");
                } else {
                    sb = kr_sbappend(sb, kr_plus(entry, ((char*(*)(void))sepEntry)()));
                }
            }
            entryStart = kr_plus(i, kr_str("1"));
        }
        i = kr_plus(i, kr_str("1"));
    }
    if (kr_truthy(kr_eq(replaced, kr_str("0")))) {
        sb = kr_sbappend(sb, kr_plus(kr_plus(kr_plus(k, ((char*(*)(void))sepKV)()), v), ((char*(*)(void))sepEntry)()));
    }
    return kr_sbtostring(sb);
}

char* jmGet(char* m, char* k) {
    char* n = kr_len(m);
    char* i = kr_str("0");
    char* entryStart = kr_str("0");
    while (kr_truthy(kr_lt(i, n))) {
        if (kr_truthy(kr_eq(kr_idx(m, kr_atoi(i)), ((char*(*)(void))sepEntry)()))) {
            char* entry = kr_substr(m, entryStart, i);
            char* sep = kr_neg(kr_str("1"));
            char* j = kr_str("0");
            while (kr_truthy(kr_lt(j, kr_len(entry)))) {
                if (kr_truthy(kr_eq(kr_idx(entry, kr_atoi(j)), ((char*(*)(void))sepKV)()))) {
                    sep = j;
                    break;
                }
                j = kr_plus(j, kr_str("1"));
            }
            if (kr_truthy(kr_gte(sep, kr_str("0")))) {
                char* kk = kr_substr(entry, kr_str("0"), sep);
                if (kr_truthy(kr_eq(kk, k))) {
                    return kr_substr(entry, kr_plus(sep, kr_str("1")), kr_len(entry));
                }
            }
            entryStart = kr_plus(i, kr_str("1"));
        }
        i = kr_plus(i, kr_str("1"));
    }
    return kr_str("");
}

char* jmHas(char* m, char* k) {
    char* n = kr_len(m);
    char* i = kr_str("0");
    char* entryStart = kr_str("0");
    while (kr_truthy(kr_lt(i, n))) {
        if (kr_truthy(kr_eq(kr_idx(m, kr_atoi(i)), ((char*(*)(void))sepEntry)()))) {
            char* entry = kr_substr(m, entryStart, i);
            char* sep = kr_neg(kr_str("1"));
            char* j = kr_str("0");
            while (kr_truthy(kr_lt(j, kr_len(entry)))) {
                if (kr_truthy(kr_eq(kr_idx(entry, kr_atoi(j)), ((char*(*)(void))sepKV)()))) {
                    sep = j;
                    break;
                }
                j = kr_plus(j, kr_str("1"));
            }
            if (kr_truthy(kr_gte(sep, kr_str("0")))) {
                char* kk = kr_substr(entry, kr_str("0"), sep);
                if (kr_truthy(kr_eq(kk, k))) {
                    return kr_str("1");
                }
            }
            entryStart = kr_plus(i, kr_str("1"));
        }
        i = kr_plus(i, kr_str("1"));
    }
    return kr_str("0");
}

char* jpParse(char* text) {
    char* m = ((char*(*)(void))jmNew)();
    char* st = ((char*(*)(char*))jpNew)(text);
    st = ((char*(*)(char*))jpSkipWS)(st);
    char* r = ((char*(*)(char*,char*,char*))jpParseValue)(st, kr_str(""), m);
    return ((char*(*)(char*))jpMap)(r);
}

char* jpGet(char* m, char* path) {
    return ((char*(*)(char*,char*))jmGet)(m, path);
}

char* jpTypeOf(char* m, char* path) {
    return ((char*(*)(char*,char*))jmGet)(m, kr_plus(kr_str("__type__\t"), path));
}

char* jpHas(char* m, char* path) {
    return ((char*(*)(char*,char*))jmHas)(m, path);
}

char* jpArrLen(char* m, char* path) {
    char* v = ((char*(*)(char*,char*))jmGet)(m, kr_plus(path, kr_str(".__len__")));
    if (kr_truthy(kr_eq(v, kr_str("")))) {
        return kr_str("0");
    }
    return kr_toint(v);
}

char* jpObjKeys(char* m, char* path) {
    return ((char*(*)(char*,char*))jmGet)(m, kr_plus(path, kr_str(".__keys__")));
}

char* jpNew(char* text) {
    return kr_plus(kr_str("0\t0\t0\t"), text);
}

char* jpHeaderField(char* st, char* idx) {
    char* n = kr_len(st);
    char* i = kr_str("0");
    char* cur = kr_str("0");
    char* start = kr_str("0");
    while (kr_truthy(kr_lt(i, n))) {
        if (kr_truthy(kr_eq(kr_idx(st, kr_atoi(i)), kr_str("\t")))) {
            if (kr_truthy(kr_eq(cur, idx))) {
                return kr_substr(st, start, i);
            }
            cur = kr_plus(cur, kr_str("1"));
            start = kr_plus(i, kr_str("1"));
            if (kr_truthy(kr_gt(cur, idx))) {
                break;
            }
        }
        i = kr_plus(i, kr_str("1"));
    }
    return kr_str("0");
}

char* jpI(char* st) {
    return kr_toint(((char*(*)(char*,char*))jpHeaderField)(st, kr_str("0")));
}

char* jpLine(char* st) {
    return kr_toint(((char*(*)(char*,char*))jpHeaderField)(st, kr_str("1")));
}

char* jpCol(char* st) {
    return kr_toint(((char*(*)(char*,char*))jpHeaderField)(st, kr_str("2")));
}

char* jpText(char* st) {
    char* i = kr_str("0");
    char* tabs = kr_str("0");
    while (kr_truthy((kr_truthy(kr_lt(i, kr_len(st))) && kr_truthy(kr_lt(tabs, kr_str("3"))) ? kr_str("1") : kr_str("0")))) {
        if (kr_truthy(kr_eq(kr_idx(st, kr_atoi(i)), kr_str("\t")))) {
            tabs = kr_plus(tabs, kr_str("1"));
        }
        i = kr_plus(i, kr_str("1"));
    }
    return kr_substr(st, i, kr_len(st));
}

char* jpAdv(char* st, char* n) {
    char* txt = ((char*(*)(char*))jpText)(st);
    char* i = ((char*(*)(char*))jpI)(st);
    char* line = ((char*(*)(char*))jpLine)(st);
    char* col = ((char*(*)(char*))jpCol)(st);
    char* k = kr_str("0");
    while (kr_truthy(kr_lt(k, n))) {
        if (kr_truthy((kr_truthy(kr_lt(i, kr_len(txt))) && kr_truthy(kr_eq(kr_idx(txt, kr_atoi(i)), kr_str("\n"))) ? kr_str("1") : kr_str("0")))) {
            line = kr_plus(line, kr_str("1"));
            col = kr_str("0");
        } else {
            col = kr_plus(col, kr_str("1"));
        }
        i = kr_plus(i, kr_str("1"));
        k = kr_plus(k, kr_str("1"));
    }
    return kr_plus(kr_plus(kr_plus(kr_plus(kr_plus(kr_plus(i, kr_str("\t")), line), kr_str("\t")), col), kr_str("\t")), txt);
}

char* jpPeek(char* st) {
    char* txt = ((char*(*)(char*))jpText)(st);
    char* i = ((char*(*)(char*))jpI)(st);
    if (kr_truthy(kr_gte(i, kr_len(txt)))) {
        return kr_str("");
    }
    return kr_idx(txt, kr_atoi(i));
}

char* jpSkipWS(char* st) {
    char* txt = ((char*(*)(char*))jpText)(st);
    char* i = ((char*(*)(char*))jpI)(st);
    while (kr_truthy(kr_lt(i, kr_len(txt)))) {
        char* c = kr_idx(txt, kr_atoi(i));
        if (kr_truthy((kr_truthy((kr_truthy((kr_truthy(kr_eq(c, kr_str(" "))) || kr_truthy(kr_eq(c, kr_str("\t"))) ? kr_str("1") : kr_str("0"))) || kr_truthy(kr_eq(c, kr_str("\n"))) ? kr_str("1") : kr_str("0"))) || kr_truthy(kr_eq(c, kr_str("\\r"))) ? kr_str("1") : kr_str("0")))) {
            st = ((char*(*)(char*,char*))jpAdv)(st, kr_str("1"));
            i = ((char*(*)(char*))jpI)(st);
            txt = ((char*(*)(char*))jpText)(st);
        } else {
            break;
        }
    }
    return st;
}

char* jpRes(char* st, char* m) {
    return kr_plus(kr_plus(st, ((char*(*)(void))sepRes)()), m);
}

char* jpStOf(char* r) {
    char* i = kr_str("0");
    while (kr_truthy(kr_lt(i, kr_len(r)))) {
        if (kr_truthy(kr_eq(kr_idx(r, kr_atoi(i)), ((char*(*)(void))sepRes)()))) {
            return kr_substr(r, kr_str("0"), i);
        }
        i = kr_plus(i, kr_str("1"));
    }
    return r;
}

char* jpMap(char* r) {
    char* i = kr_str("0");
    while (kr_truthy(kr_lt(i, kr_len(r)))) {
        if (kr_truthy(kr_eq(kr_idx(r, kr_atoi(i)), ((char*(*)(void))sepRes)()))) {
            return kr_substr(r, kr_plus(i, kr_str("1")), kr_len(r));
        }
        i = kr_plus(i, kr_str("1"));
    }
    return kr_str("");
}

char* jpRecord(char* m, char* path, char* typ, char* val) {
    m = ((char*(*)(char*,char*,char*))jmSet)(m, path, val);
    m = ((char*(*)(char*,char*,char*))jmSet)(m, kr_plus(kr_str("__type__\t"), path), typ);
    return m;
}

char* jpParseValue(char* st, char* path, char* m) {
    st = ((char*(*)(char*))jpSkipWS)(st);
    char* c = ((char*(*)(char*))jpPeek)(st);
    if (kr_truthy(kr_eq(c, kr_str("\"")))) {
        return ((char*(*)(char*,char*,char*))jpParseString)(st, path, m);
    }
    if (kr_truthy(kr_eq(c, kr_str("{")))) {
        return ((char*(*)(char*,char*,char*))jpParseObject)(st, path, m);
    }
    if (kr_truthy(kr_eq(c, kr_str("[")))) {
        return ((char*(*)(char*,char*,char*))jpParseArray)(st, path, m);
    }
    if (kr_truthy((kr_truthy(kr_eq(c, kr_str("t"))) || kr_truthy(kr_eq(c, kr_str("f"))) ? kr_str("1") : kr_str("0")))) {
        return ((char*(*)(char*,char*,char*))jpParseBool)(st, path, m);
    }
    if (kr_truthy(kr_eq(c, kr_str("n")))) {
        return ((char*(*)(char*,char*,char*))jpParseNull)(st, path, m);
    }
    if (kr_truthy((kr_truthy(kr_eq(c, kr_str("-"))) || kr_truthy((kr_truthy(kr_gte(c, kr_str("0"))) && kr_truthy(kr_lte(c, kr_str("9"))) ? kr_str("1") : kr_str("0"))) ? kr_str("1") : kr_str("0")))) {
        return ((char*(*)(char*,char*,char*))jpParseNumber)(st, path, m);
    }
    st = ((char*(*)(char*,char*))jpAdv)(st, kr_str("1"));
    return ((char*(*)(char*,char*))jpRes)(st, m);
}

char* jpParseString(char* st, char* path, char* m) {
    st = ((char*(*)(char*,char*))jpAdv)(st, kr_str("1"));
    char* txt = ((char*(*)(char*))jpText)(st);
    char* i = ((char*(*)(char*))jpI)(st);
    char* sb = kr_sbnew();
    while (kr_truthy(kr_lt(i, kr_len(txt)))) {
        char* c = kr_idx(txt, kr_atoi(i));
        if (kr_truthy(kr_eq(c, kr_str("\"")))) {
            st = ((char*(*)(char*,char*))jpAdv)(st, kr_plus(kr_sub(i, ((char*(*)(char*))jpI)(st)), kr_str("1")));
            break;
        }
        if (kr_truthy((kr_truthy(kr_eq(c, kr_str("\\"))) && kr_truthy(kr_lt(kr_plus(i, kr_str("1")), kr_len(txt))) ? kr_str("1") : kr_str("0")))) {
            char* esc = kr_idx(txt, kr_atoi(kr_plus(i, kr_str("1"))));
            if (kr_truthy(kr_eq(esc, kr_str("\"")))) {
                sb = kr_sbappend(sb, kr_str("\""));
            } else if (kr_truthy(kr_eq(esc, kr_str("\\")))) {
                sb = kr_sbappend(sb, kr_str("\\"));
            } else if (kr_truthy(kr_eq(esc, kr_str("/")))) {
                sb = kr_sbappend(sb, kr_str("/"));
            } else if (kr_truthy(kr_eq(esc, kr_str("n")))) {
                sb = kr_sbappend(sb, kr_str("\n"));
            } else if (kr_truthy(kr_eq(esc, kr_str("t")))) {
                sb = kr_sbappend(sb, kr_str("\t"));
            } else if (kr_truthy(kr_eq(esc, kr_str("r")))) {
                sb = kr_sbappend(sb, kr_str("\\r"));
            } else if (kr_truthy(kr_eq(esc, kr_str("b")))) {
                sb = kr_sbappend(sb, kr_str("\\b"));
            } else if (kr_truthy(kr_eq(esc, kr_str("f")))) {
                sb = kr_sbappend(sb, kr_str("\\f"));
            } else if (kr_truthy((kr_truthy(kr_eq(esc, kr_str("u"))) && kr_truthy(kr_lt(kr_plus(i, kr_str("5")), kr_len(txt))) ? kr_str("1") : kr_str("0")))) {
                char* h = kr_substr(txt, kr_plus(i, kr_str("2")), kr_plus(i, kr_str("6")));
                char* v = kr_str("0");
                char* k = kr_str("0");
                while (kr_truthy(kr_lt(k, kr_str("4")))) {
                    char* hc = kr_idx(h, kr_atoi(k));
                    char* d = kr_str("0");
                    if (kr_truthy((kr_truthy(kr_gte(hc, kr_str("0"))) && kr_truthy(kr_lte(hc, kr_str("9"))) ? kr_str("1") : kr_str("0")))) {
                        d = kr_sub(kr_charcode(hc), kr_charcode(kr_str("0")));
                    } else if (kr_truthy((kr_truthy(kr_gte(hc, kr_str("a"))) && kr_truthy(kr_lte(hc, kr_str("f"))) ? kr_str("1") : kr_str("0")))) {
                        d = kr_plus(kr_sub(kr_charcode(hc), kr_charcode(kr_str("a"))), kr_str("10"));
                    } else if (kr_truthy((kr_truthy(kr_gte(hc, kr_str("A"))) && kr_truthy(kr_lte(hc, kr_str("F"))) ? kr_str("1") : kr_str("0")))) {
                        d = kr_plus(kr_sub(kr_charcode(hc), kr_charcode(kr_str("A"))), kr_str("10"));
                    }
                    v = kr_plus(kr_mul(v, kr_str("16")), d);
                    k = kr_plus(k, kr_str("1"));
                }
                if (kr_truthy(kr_lt(v, kr_str("128")))) {
                    sb = kr_sbappend(sb, kr_fromcharcode(v));
                }
                i = kr_plus(i, kr_str("4"));
            } else {
                sb = kr_sbappend(sb, esc);
            }
            i = kr_plus(i, kr_str("2"));
            continue;
        }
        sb = kr_sbappend(sb, c);
        i = kr_plus(i, kr_str("1"));
    }
    if (kr_truthy(kr_neq(path, kr_str("")))) {
        m = ((char*(*)(char*,char*,char*,char*))jpRecord)(m, path, kr_str("s"), kr_sbtostring(sb));
    }
    return ((char*(*)(char*,char*))jpRes)(st, m);
}

char* jpParseNumber(char* st, char* path, char* m) {
    char* txt = ((char*(*)(char*))jpText)(st);
    char* start = ((char*(*)(char*))jpI)(st);
    char* i = start;
    if (kr_truthy((kr_truthy(kr_lt(i, kr_len(txt))) && kr_truthy(kr_eq(kr_idx(txt, kr_atoi(i)), kr_str("-"))) ? kr_str("1") : kr_str("0")))) {
        i = kr_plus(i, kr_str("1"));
    }
    while (kr_truthy((kr_truthy((kr_truthy(kr_lt(i, kr_len(txt))) && kr_truthy(kr_gte(kr_idx(txt, kr_atoi(i)), kr_str("0"))) ? kr_str("1") : kr_str("0"))) && kr_truthy(kr_lte(kr_idx(txt, kr_atoi(i)), kr_str("9"))) ? kr_str("1") : kr_str("0")))) {
        i = kr_plus(i, kr_str("1"));
    }
    if (kr_truthy((kr_truthy(kr_lt(i, kr_len(txt))) && kr_truthy(kr_eq(kr_idx(txt, kr_atoi(i)), kr_str("."))) ? kr_str("1") : kr_str("0")))) {
        i = kr_plus(i, kr_str("1"));
        while (kr_truthy((kr_truthy((kr_truthy(kr_lt(i, kr_len(txt))) && kr_truthy(kr_gte(kr_idx(txt, kr_atoi(i)), kr_str("0"))) ? kr_str("1") : kr_str("0"))) && kr_truthy(kr_lte(kr_idx(txt, kr_atoi(i)), kr_str("9"))) ? kr_str("1") : kr_str("0")))) {
            i = kr_plus(i, kr_str("1"));
        }
    }
    if (kr_truthy((kr_truthy(kr_lt(i, kr_len(txt))) && kr_truthy((kr_truthy(kr_eq(kr_idx(txt, kr_atoi(i)), kr_str("e"))) || kr_truthy(kr_eq(kr_idx(txt, kr_atoi(i)), kr_str("E"))) ? kr_str("1") : kr_str("0"))) ? kr_str("1") : kr_str("0")))) {
        i = kr_plus(i, kr_str("1"));
        if (kr_truthy((kr_truthy(kr_lt(i, kr_len(txt))) && kr_truthy((kr_truthy(kr_eq(kr_idx(txt, kr_atoi(i)), kr_str("+"))) || kr_truthy(kr_eq(kr_idx(txt, kr_atoi(i)), kr_str("-"))) ? kr_str("1") : kr_str("0"))) ? kr_str("1") : kr_str("0")))) {
            i = kr_plus(i, kr_str("1"));
        }
        while (kr_truthy((kr_truthy((kr_truthy(kr_lt(i, kr_len(txt))) && kr_truthy(kr_gte(kr_idx(txt, kr_atoi(i)), kr_str("0"))) ? kr_str("1") : kr_str("0"))) && kr_truthy(kr_lte(kr_idx(txt, kr_atoi(i)), kr_str("9"))) ? kr_str("1") : kr_str("0")))) {
            i = kr_plus(i, kr_str("1"));
        }
    }
    char* val = kr_substr(txt, start, i);
    st = ((char*(*)(char*,char*))jpAdv)(st, kr_sub(i, start));
    if (kr_truthy(kr_neq(path, kr_str("")))) {
        m = ((char*(*)(char*,char*,char*,char*))jpRecord)(m, path, kr_str("n"), val);
    }
    return ((char*(*)(char*,char*))jpRes)(st, m);
}

char* jpParseBool(char* st, char* path, char* m) {
    char* txt = ((char*(*)(char*))jpText)(st);
    char* i = ((char*(*)(char*))jpI)(st);
    if (kr_truthy((kr_truthy(kr_lte(kr_plus(i, kr_str("4")), kr_len(txt))) && kr_truthy(kr_eq(kr_substr(txt, i, kr_plus(i, kr_str("4"))), kr_str("true"))) ? kr_str("1") : kr_str("0")))) {
        st = ((char*(*)(char*,char*))jpAdv)(st, kr_str("4"));
        if (kr_truthy(kr_neq(path, kr_str("")))) {
            m = ((char*(*)(char*,char*,char*,char*))jpRecord)(m, path, kr_str("b"), kr_str("true"));
        }
        return ((char*(*)(char*,char*))jpRes)(st, m);
    }
    if (kr_truthy((kr_truthy(kr_lte(kr_plus(i, kr_str("5")), kr_len(txt))) && kr_truthy(kr_eq(kr_substr(txt, i, kr_plus(i, kr_str("5"))), kr_str("false"))) ? kr_str("1") : kr_str("0")))) {
        st = ((char*(*)(char*,char*))jpAdv)(st, kr_str("5"));
        if (kr_truthy(kr_neq(path, kr_str("")))) {
            m = ((char*(*)(char*,char*,char*,char*))jpRecord)(m, path, kr_str("b"), kr_str("false"));
        }
        return ((char*(*)(char*,char*))jpRes)(st, m);
    }
    return ((char*(*)(char*,char*))jpRes)(st, m);
}

char* jpParseNull(char* st, char* path, char* m) {
    char* txt = ((char*(*)(char*))jpText)(st);
    char* i = ((char*(*)(char*))jpI)(st);
    if (kr_truthy((kr_truthy(kr_lte(kr_plus(i, kr_str("4")), kr_len(txt))) && kr_truthy(kr_eq(kr_substr(txt, i, kr_plus(i, kr_str("4"))), kr_str("null"))) ? kr_str("1") : kr_str("0")))) {
        st = ((char*(*)(char*,char*))jpAdv)(st, kr_str("4"));
        if (kr_truthy(kr_neq(path, kr_str("")))) {
            m = ((char*(*)(char*,char*,char*,char*))jpRecord)(m, path, kr_str("z"), kr_str(""));
        }
    }
    return ((char*(*)(char*,char*))jpRes)(st, m);
}

char* jpJoin(char* parent, char* child) {
    if (kr_truthy(kr_eq(parent, kr_str("")))) {
        return child;
    }
    return kr_plus(kr_plus(parent, kr_str(".")), child);
}

char* jpParseObject(char* st, char* path, char* m) {
    st = ((char*(*)(char*,char*))jpAdv)(st, kr_str("1"));
    st = ((char*(*)(char*))jpSkipWS)(st);
    char* keys = kr_str("");
    if (kr_truthy(kr_neq(path, kr_str("")))) {
        m = ((char*(*)(char*,char*,char*))jmSet)(m, kr_plus(kr_str("__type__\t"), path), kr_str("obj"));
    }
    if (kr_truthy(kr_eq(((char*(*)(char*))jpPeek)(st), kr_str("}")))) {
        st = ((char*(*)(char*,char*))jpAdv)(st, kr_str("1"));
        if (kr_truthy(kr_neq(path, kr_str("")))) {
            m = ((char*(*)(char*,char*,char*))jmSet)(m, kr_plus(path, kr_str(".__keys__")), keys);
        }
        return ((char*(*)(char*,char*))jpRes)(st, m);
    }
    while (kr_truthy(kr_lt(((char*(*)(char*))jpI)(st), kr_len(((char*(*)(char*))jpText)(st))))) {
        st = ((char*(*)(char*))jpSkipWS)(st);
        if (kr_truthy(kr_neq(((char*(*)(char*))jpPeek)(st), kr_str("\"")))) {
            break;
        }
        st = ((char*(*)(char*,char*))jpAdv)(st, kr_str("1"));
        char* txt = ((char*(*)(char*))jpText)(st);
        char* ki = ((char*(*)(char*))jpI)(st);
        char* kSb = kr_sbnew();
        while (kr_truthy(kr_lt(ki, kr_len(txt)))) {
            char* c = kr_idx(txt, kr_atoi(ki));
            if (kr_truthy(kr_eq(c, kr_str("\"")))) {
                break;
            }
            if (kr_truthy((kr_truthy(kr_eq(c, kr_str("\\"))) && kr_truthy(kr_lt(kr_plus(ki, kr_str("1")), kr_len(txt))) ? kr_str("1") : kr_str("0")))) {
                kSb = kr_sbappend(kSb, kr_idx(txt, kr_atoi(kr_plus(ki, kr_str("1")))));
                ki = kr_plus(ki, kr_str("2"));
                continue;
            }
            kSb = kr_sbappend(kSb, c);
            ki = kr_plus(ki, kr_str("1"));
        }
        char* keyName = kr_sbtostring(kSb);
        st = ((char*(*)(char*,char*))jpAdv)(st, kr_plus(kr_sub(ki, ((char*(*)(char*))jpI)(st)), kr_str("1")));
        st = ((char*(*)(char*))jpSkipWS)(st);
        if (kr_truthy(kr_eq(((char*(*)(char*))jpPeek)(st), kr_str(":")))) {
            st = ((char*(*)(char*,char*))jpAdv)(st, kr_str("1"));
        }
        st = ((char*(*)(char*))jpSkipWS)(st);
        char* childPath = ((char*(*)(char*,char*))jpJoin)(path, keyName);
        char* r = ((char*(*)(char*,char*,char*))jpParseValue)(st, childPath, m);
        st = ((char*(*)(char*))jpStOf)(r);
        m = ((char*(*)(char*))jpMap)(r);
        if (kr_truthy(kr_eq(keys, kr_str("")))) {
            keys = keyName;
        } else {
            keys = kr_plus(kr_plus(keys, kr_str(",")), keyName);
        }
        st = ((char*(*)(char*))jpSkipWS)(st);
        if (kr_truthy(kr_eq(((char*(*)(char*))jpPeek)(st), kr_str(",")))) {
            st = ((char*(*)(char*,char*))jpAdv)(st, kr_str("1"));
            continue;
        }
        if (kr_truthy(kr_eq(((char*(*)(char*))jpPeek)(st), kr_str("}")))) {
            st = ((char*(*)(char*,char*))jpAdv)(st, kr_str("1"));
            break;
        }
        break;
    }
    if (kr_truthy(kr_neq(path, kr_str("")))) {
        m = ((char*(*)(char*,char*,char*))jmSet)(m, kr_plus(path, kr_str(".__keys__")), keys);
    }
    return ((char*(*)(char*,char*))jpRes)(st, m);
}

char* jpParseArray(char* st, char* path, char* m) {
    st = ((char*(*)(char*,char*))jpAdv)(st, kr_str("1"));
    st = ((char*(*)(char*))jpSkipWS)(st);
    char* idx = kr_str("0");
    if (kr_truthy(kr_neq(path, kr_str("")))) {
        m = ((char*(*)(char*,char*,char*))jmSet)(m, kr_plus(kr_str("__type__\t"), path), kr_str("arr"));
    }
    if (kr_truthy(kr_eq(((char*(*)(char*))jpPeek)(st), kr_str("]")))) {
        st = ((char*(*)(char*,char*))jpAdv)(st, kr_str("1"));
        if (kr_truthy(kr_neq(path, kr_str("")))) {
            m = ((char*(*)(char*,char*,char*))jmSet)(m, kr_plus(path, kr_str(".__len__")), kr_str("0"));
        }
        return ((char*(*)(char*,char*))jpRes)(st, m);
    }
    while (kr_truthy(kr_lt(((char*(*)(char*))jpI)(st), kr_len(((char*(*)(char*))jpText)(st))))) {
        st = ((char*(*)(char*))jpSkipWS)(st);
        char* childPath = ((char*(*)(char*,char*))jpJoin)(path, kr_plus(idx, kr_str("")));
        char* r = ((char*(*)(char*,char*,char*))jpParseValue)(st, childPath, m);
        st = ((char*(*)(char*))jpStOf)(r);
        m = ((char*(*)(char*))jpMap)(r);
        idx = kr_plus(idx, kr_str("1"));
        st = ((char*(*)(char*))jpSkipWS)(st);
        if (kr_truthy(kr_eq(((char*(*)(char*))jpPeek)(st), kr_str(",")))) {
            st = ((char*(*)(char*,char*))jpAdv)(st, kr_str("1"));
            continue;
        }
        if (kr_truthy(kr_eq(((char*(*)(char*))jpPeek)(st), kr_str("]")))) {
            st = ((char*(*)(char*,char*))jpAdv)(st, kr_str("1"));
            break;
        }
        break;
    }
    if (kr_truthy(kr_neq(path, kr_str("")))) {
        m = ((char*(*)(char*,char*,char*))jmSet)(m, kr_plus(path, kr_str(".__len__")), kr_plus(idx, kr_str("")));
    }
    return ((char*(*)(char*,char*))jpRes)(st, m);
}

char* bExecStream(char* cmd, char* sinkPtr) {
    return ((char*(*)(char*,char*))krBuildExec)(cmd, sinkPtr);
}

char* sendResult(char*, char*);
char* sendError(char*, char*, char*);
char* sendNotify(char*, char*);
char* buildSink(char*);
char* capabilities();
char* handleBuildRun(char*, char*);
char* handle(char*, char*, char*);

char* sendResult(char* id, char* result) {
    char* msg = ((char*(*)(char*,char*,char*,char*,char*,char*))jeObj3)(kr_str("jsonrpc"), ((char*(*)(char*))jeStr)(kr_str("2.0")), kr_str("id"), ((char*(*)(char*))jeRaw)(id), kr_str("result"), result);
    ((char*(*)(char*))jrWriteFrame)(msg);
}

char* sendError(char* id, char* code, char* message) {
    char* err = ((char*(*)(char*,char*,char*,char*))jeObj2)(kr_str("code"), ((char*(*)(char*))jeNum)(code), kr_str("message"), ((char*(*)(char*))jeStr)(message));
    char* msg = ((char*(*)(char*,char*,char*,char*,char*,char*))jeObj3)(kr_str("jsonrpc"), ((char*(*)(char*))jeStr)(kr_str("2.0")), kr_str("id"), ((char*(*)(char*))jeRaw)(id), kr_str("error"), err);
    ((char*(*)(char*))jrWriteFrame)(msg);
}

char* sendNotify(char* method, char* params) {
    char* msg = ((char*(*)(char*,char*,char*,char*,char*,char*))jeObj3)(kr_str("jsonrpc"), ((char*(*)(char*))jeStr)(kr_str("2.0")), kr_str("method"), ((char*(*)(char*))jeStr)(method), kr_str("params"), params);
    ((char*(*)(char*))jrWriteFrame)(msg);
}

char* buildSink(char* chunk) {
    char* params = ((char*(*)(char*,char*))jeObj)(kr_str("chunk"), ((char*(*)(char*))jeStr)(chunk));
    ((char*(*)(char*,char*))sendNotify)(kr_str("build/output"), params);
    return kr_str("");
}

char* capabilities() {
    char* caps = ((char*(*)(char*,char*,char*,char*,char*,char*,char*,char*))jeObj4)(kr_str("buildRun"), ((char*(*)(char*))jeRaw)(kr_str("true")), kr_str("streamingOutput"), ((char*(*)(char*))jeRaw)(kr_str("true")), kr_str("memorySample"), ((char*(*)(char*))jeRaw)(kr_str("false")), kr_str("lspProxy"), ((char*(*)(char*))jeRaw)(kr_str("false")));
    char* info = ((char*(*)(char*,char*,char*,char*))jeObj2)(kr_str("name"), ((char*(*)(char*))jeStr)(kr_str("kbackend")), kr_str("version"), ((char*(*)(char*))jeStr)(kr_str("0.1.0")));
    return ((char*(*)(char*,char*,char*,char*))jeObj2)(kr_str("capabilities"), caps, kr_str("serverInfo"), info);
}

char* handleBuildRun(char* idJson, char* parsed) {
    char* cmd = ((char*(*)(char*,char*))jpGet)(parsed, kr_str("params.cmd"));
    if (kr_truthy(kr_eq(cmd, kr_str("")))) {
        ((char*(*)(char*,char*,char*))sendError)(idJson, kr_neg(kr_str("32602")), kr_str("build/run requires params.cmd"));
        return kr_str("0");
    }
    ((char*(*)(char*))jrLog)(kr_plus(kr_str("build/run: "), cmd));
    char* sink = ((char*)buildSink);
    char* codeStr = ((char*(*)(char*,char*))bExecStream)(cmd, sink);
    ((char*(*)(char*,char*))sendNotify)(kr_str("build/finished"), ((char*(*)(char*,char*))jeObj)(kr_str("code"), ((char*(*)(char*))jeNum)(kr_toint(codeStr))));
    ((char*(*)(char*,char*))sendResult)(idJson, ((char*(*)(char*,char*))jeObj)(kr_str("code"), ((char*(*)(char*))jeNum)(kr_toint(codeStr))));
    return kr_str("0");
}

char* handle(char* method, char* idJson, char* parsed) {
    if (kr_truthy(kr_eq(method, kr_str("initialize")))) {
        ((char*(*)(char*,char*))sendResult)(idJson, ((char*(*)(void))capabilities)());
        return kr_str("0");
    }
    if (kr_truthy(kr_eq(method, kr_str("initialized")))) {
        return kr_str("0");
    }
    if (kr_truthy(kr_eq(method, kr_str("ping")))) {
        ((char*(*)(char*,char*))sendResult)(idJson, ((char*(*)(char*,char*))jeObj)(kr_str("pong"), ((char*(*)(char*))jeRaw)(kr_str("true"))));
        return kr_str("0");
    }
    if (kr_truthy(kr_eq(method, kr_str("build/run")))) {
        ((char*(*)(char*,char*))handleBuildRun)(idJson, parsed);
        return kr_str("0");
    }
    if (kr_truthy(kr_eq(method, kr_str("shutdown")))) {
        ((char*(*)(char*,char*))sendResult)(idJson, ((char*(*)(void))jeNull)());
        return kr_str("0");
    }
    if (kr_truthy(kr_eq(method, kr_str("exit")))) {
        return kr_str("0");
    }
    if (kr_truthy((kr_truthy(kr_neq(idJson, kr_str(""))) && kr_truthy(kr_neq(idJson, kr_str("null"))) ? kr_str("1") : kr_str("0")))) {
        ((char*(*)(char*,char*,char*))sendError)(idJson, kr_neg(kr_str("32601")), kr_plus(kr_str("method not found: "), method));
    }
    return kr_str("0");
}

int main(int argc, char** argv) {
    _argc = argc; _argv = argv;
    srand((unsigned)time(NULL));
    ((char*(*)(void))jrInit)();
    ((char*(*)(char*))jrLog)(kr_str("kbackend 0.1.0 starting"));
    char* running = kr_str("1");
    while (kr_truthy(kr_eq(running, kr_str("1")))) {
        char* frame = ((char*(*)(void))jrReadFrame)();
        if (kr_truthy(kr_eq(frame, kr_str("")))) {
            ((char*(*)(char*))jrLog)(kr_str("EOF on stdin, exiting"));
            running = kr_str("0");
        } else {
            char* parsed = ((char*(*)(char*))jpParse)(frame);
            char* method = ((char*(*)(char*,char*))jpGet)(parsed, kr_str("method"));
            char* idType = ((char*(*)(char*,char*))jpTypeOf)(parsed, kr_str("id"));
            char* idJson = kr_str("");
            if (kr_truthy(kr_eq(idType, kr_str("n")))) {
                idJson = ((char*(*)(char*,char*))jpGet)(parsed, kr_str("id"));
            }
            if (kr_truthy(kr_eq(idType, kr_str("s")))) {
                idJson = ((char*(*)(char*))jeStr)(((char*(*)(char*,char*))jpGet)(parsed, kr_str("id")));
            }
            ((char*(*)(char*))jrLog)(kr_plus(kr_str("method: "), method));
            if (kr_truthy(kr_eq(method, kr_str("exit")))) {
                running = kr_str("0");
            } else {
                ((char*(*)(char*,char*,char*))handle)(method, idJson, parsed);
            }
        }
    }
    ((char*(*)(char*))jrLog)(kr_str("kbackend bye"));
    return 0;
}

