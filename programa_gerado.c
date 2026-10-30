#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <stdint.h>

/* Biblioteca padrão Cbr 1.0 Estável: texto, matemática, arrays e controle. */
typedef enum { CBR_INT, CBR_FLOAT, CBR_BOOL, CBR_STRING } CbrType;
typedef struct { CbrType type; union { long long i; double f; bool b; const char *s; } value; } CbrValue;
static inline CbrValue cbr_int(long long x) { CbrValue v; v.type=CBR_INT; v.value.i=x; return v; }
static inline CbrValue cbr_float(double x) { CbrValue v; v.type=CBR_FLOAT; v.value.f=x; return v; }
static inline CbrValue cbr_bool(bool x) { CbrValue v; v.type=CBR_BOOL; v.value.b=x; return v; }
static inline CbrValue cbr_string(const char *x) { CbrValue v; v.type=CBR_STRING; v.value.s=x; return v; }
static inline CbrValue cbr_at(const CbrValue *a, size_t n, long long i) { if(i<0 || (size_t)i>=n) { fprintf(stderr, "Cbr: índice de array fora do limite\n"); exit(1); } return a[i]; }
static inline void cbr_set(CbrValue *a, size_t n, long long i, CbrValue v) { if(i<0 || (size_t)i>=n) { fprintf(stderr, "Cbr: índice de array fora do limite\n"); exit(1); } a[i]=v; }
static inline void cbr_append(CbrValue *a, size_t *n, size_t capacity, CbrValue v) { if(*n>=capacity) { fprintf(stderr, "Cbr: capacidade máxima do array atingida\n"); exit(1); } a[(*n)++]=v; }
static inline void cbr_remove(CbrValue *a, size_t *n, long long i) { if(i<0 || (size_t)i>=*n) { fprintf(stderr, "Cbr: índice de remoção fora do limite\n"); exit(1); } for(size_t j=(size_t)i;j+1<*n;j++) a[j]=a[j+1]; (*n)--; }
static inline long long cbr_as_int(CbrValue v) { if(v.type!=CBR_INT) { fprintf(stderr, "Cbr: elemento não é int\n"); exit(1); } return v.value.i; }
static inline double cbr_as_float(CbrValue v) { if(v.type==CBR_INT) return (double)v.value.i; if(v.type!=CBR_FLOAT) { fprintf(stderr, "Cbr: elemento não é float\n"); exit(1); } return v.value.f; }
static inline bool cbr_as_bool(CbrValue v) { if(v.type!=CBR_BOOL) { fprintf(stderr, "Cbr: elemento não é bool\n"); exit(1); } return v.value.b; }
static inline const char *cbr_as_string(CbrValue v) { if(v.type!=CBR_STRING) { fprintf(stderr, "Cbr: elemento não é str\n"); exit(1); } return v.value.s; }

static inline size_t cbr_length(const char *s) { return strlen(s ? s : ""); }
static inline const char *cbr_join(const char *a, const char *b) { size_t x=strlen(a), y=strlen(b); char *r=(char*)malloc(x+y+1); if(!r){perror("malloc"); exit(1);} memcpy(r,a,x); memcpy(r+x,b,y+1); return r; }
static inline const char *cbr_substr(const char *s, long long start, long long count) { size_t n=strlen(s?s:""); if(start<0) start=0; if((size_t)start>n) start=(long long)n; if(count<0) count=0; size_t available=n-(size_t)start; size_t take=(size_t)count<available?(size_t)count:available; char *r=(char*)malloc(take+1); if(!r){perror("malloc"); exit(1);} memcpy(r,s+start,take); r[take]=0; return r; }
static inline const char *cbr_replace(const char *s, const char *old, const char *replacement) { if(!s||!old||!replacement) return s?s:""; size_t oldn=strlen(old), newn=strlen(replacement); if(oldn==0) return s; size_t count=0; const char *p=s; while((p=strstr(p,old))!=NULL){count++;p+=oldn;} size_t srcn=strlen(s); if(newn>oldn && count>(SIZE_MAX-srcn-1)/(newn-oldn)){fprintf(stderr,"Cbr: texto substituído grande demais\n");exit(1);} size_t outn=newn>=oldn?srcn+count*(newn-oldn):srcn-count*(oldn-newn); char *r=(char*)malloc(outn+1); if(!r){perror("malloc");exit(1);} const char *cur=s; char *dst=r; while((p=strstr(cur,old))!=NULL){size_t n=(size_t)(p-cur);memcpy(dst,cur,n);dst+=n;memcpy(dst,replacement,newn);dst+=newn;cur=p+oldn;} strcpy(dst,cur); return r; }
static inline const char *cbr_case(const char *s, int upper) { size_t n=strlen(s); char *r=(char*)malloc(n+1); if(!r){perror("malloc"); exit(1);} for(size_t i=0;i<n;i++) r[i]=(char)(upper?toupper((unsigned char)s[i]):tolower((unsigned char)s[i])); r[n]=0; return r; }
#define cbr_upper(s) cbr_case((s), 1)
#define cbr_lower(s) cbr_case((s), 0)

static double dobro(double valor);
static double calcular_pontos(double base, double bonus);

static double dobro(double valor) {
    return (double)(valor * 2);
}

static double calcular_pontos(double base, double bonus) {
    return (double)(base * 10 + bonus);
}

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

int main(void) {
    const char *nome = "";
    int idade = 0;
    double nota = 0.0;
    bool estudante = true;
    printf("Digite seu nome: ");
    static char __cbr_scanbuf_17[4096];
    if (!fgets(__cbr_scanbuf_17, sizeof __cbr_scanbuf_17, stdin)) { fprintf(stderr, "Erro ao ler entrada.\n"); return 1; }
    __cbr_scanbuf_17[strcspn(__cbr_scanbuf_17, "\n")] = '\0';
    nome = __cbr_scanbuf_17;
    printf("Digite sua idade: ");
    static char __cbr_scanbuf_18[4096];
    if (!fgets(__cbr_scanbuf_18, sizeof __cbr_scanbuf_18, stdin)) { fprintf(stderr, "Erro ao ler entrada.\n"); return 1; }
    __cbr_scanbuf_18[strcspn(__cbr_scanbuf_18, "\n")] = '\0';
    if (sscanf(__cbr_scanbuf_18, "%d", &idade) != 1) { fprintf(stderr, "Entrada inválida para inteiro.\n"); return 1; }
    printf("Digite sua nota: ");
    static char __cbr_scanbuf_19[4096];
    if (!fgets(__cbr_scanbuf_19, sizeof __cbr_scanbuf_19, stdin)) { fprintf(stderr, "Erro ao ler entrada.\n"); return 1; }
    __cbr_scanbuf_19[strcspn(__cbr_scanbuf_19, "\n")] = '\0';
    if (sscanf(__cbr_scanbuf_19, "%lf", &nota) != 1) { fprintf(stderr, "Entrada inválida para decimal.\n"); return 1; }
    int proxima_idade = idade + 1;
    double idade_dobrada = dobro(idade);
    double pontos = calcular_pontos(idade, 5);
    int resto = idade % 2;
    int potencia = pow((2), (3));
    bool aprovado = nota >= 6.0 && estudante;
    const char * nome_completo = cbr_join(nome, " - Cbr 1.0 BETA");
    const char * nome_maiusculo = cbr_upper(nome_completo);
    const char * nome_minusculo = cbr_lower(nome_completo);
    const char * trecho = cbr_substr(nome_completo, 0, 5);
    const char * nome_trocado = cbr_replace(nome_completo, "Cbr", "C brasileiro");
    bool tem_freud = (strstr(nome_minusculo, "freud") != NULL);
    int tamanho_nome = (int)cbr_length(nome_completo);
    double raiz_quadrada = sqrt((((double)(81))));
    double valor_absoluto = fabs((((double)(-5))));
    double valor_arredondado = round((((double)(3.6))));
    double valor_piso = floor((((double)(3.9))));
    double valor_teto = ceil((((double)(3.1))));
    double valor_seno = sin((((double)(0))));
    double valor_cosseno = cos((((double)(0))));
    double valor_maximo = fmax((((double)(idade))), (((double)(20))));
    double valor_minimo = fmin((((double)(idade))), (((double)(20))));
    double valor_limitado = fmax((((double)(0))), fmin((((double)(18))), (((double)(idade)))));
    CbrValue materiais[256] = {cbr_int(12), cbr_string("Cbr"), cbr_bool(true), cbr_float(3.14), cbr_int(-7)};
    size_t materiais_len = 5;
    cbr_set(materiais, materiais_len, 0, cbr_int(42));
    cbr_append(materiais, &materiais_len, 256, cbr_float(9.5));
    cbr_append(materiais, &materiais_len, 256, cbr_string("fim"));
    cbr_remove(materiais, &materiais_len, 1);
    int quantidade_materiais = (int)materiais_len;
    printf("\n=== RELATORIO CBR 1.0 BETA ===\n");
    printf("Nome: %s\n", nome);
    printf("Idade: %d; proximo ano: %d; dobro: %g\n", idade, proxima_idade, idade_dobrada);
    printf("Pontos: %g; resto: %d; 2**3: %d\n", pontos, resto, potencia);
    printf("Texto: %s\n", nome_completo);
    printf("Maiusculas: %s\n", nome_maiusculo);
    printf("Minusculas: %s\n", nome_minusculo);
    printf("Trecho: %s; substituido: %s\n", trecho, nome_trocado);
    printf("Contem Freud: %s; tamanho: %d\n", (tem_freud ? "true" : "false"), tamanho_nome);
    printf("Math: raiz=%g, abs=%g, round=%g, piso=%g, teto=%g\n", raiz_quadrada, valor_absoluto, valor_arredondado, valor_piso, valor_teto);
    printf("Trigonometria: seno=%g, cosseno=%g\n", valor_seno, valor_cosseno);
    printf("Min=%g, max=%g, limitar=%g\n", valor_minimo, valor_maximo, valor_limitado);
    printf("Array: %lld, %s, %g, %lld\n", cbr_as_int(cbr_at(materiais, materiais_len, 0)), (cbr_as_bool(cbr_at(materiais, materiais_len, 1)) ? "true" : "false"), cbr_as_float(cbr_at(materiais, materiais_len, 2)), cbr_as_int(cbr_at(materiais, materiais_len, 3)));
    printf("Quantidade do array: %d\n", quantidade_materiais);
    if (aprovado) {
    printf("Resultado: aprovado\n");
    } else {
    printf("Resultado: estudar mais\n");
    }
    if (idade >= 18) {
    printf("Faixa: adulto\n");
    } else {
    printf("Faixa: menor de idade\n");
    }
    switch (resto) {
    case 0:
    printf("A idade e par\n");
    break;
    case 1:
    printf("A idade e impar\n");
    break;
    default:
    printf("Paridade desconhecida\n");
    }
    int contador = 0;
    while (contador < 3) {
    printf("While: %d\n", contador);
    contador = contador + 1;
    }
    printf("For range: ");
    for (int i = 0; i < (5); i++) {
    if (i == 1) {
    continue;
    }
    printf("%d ", i);
    }
    printf("\n");
    printf("For classico: ");
    for (int j = 0; j < 3; j = j + 1) {
    printf("%d ", j);
    }
    printf("\n");
    return 0;
}
