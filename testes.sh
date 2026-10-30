#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
CC=${CC:-gcc}
CFLAGS='-std=c11 -Wall -Wextra -Werror -O2'

$CC $CFLAGS "$ROOT/cbr.c" -lm -o "$TMP/cbr"

"$TMP/cbr" "$ROOT/tests/biblioteca.cbr" "$TMP/biblioteca.c"
$CC $CFLAGS "$TMP/biblioteca.c" -lm -o "$TMP/biblioteca"
"$TMP/biblioteca" > "$TMP/biblioteca.out"
grep -F 'texto=ana; troca=baXX; contem=true' "$TMP/biblioteca.out"
grep -F 'math=9,5,4,20,10' "$TMP/biblioteca.out"
grep -F 'array=99,3.5; n=3; vazio=ok; n2=1' "$TMP/biblioteca.out"
grep -F 'limpo=0' "$TMP/biblioteca.out"

"$TMP/cbr" "$ROOT/tests/funcoes_switch.cbr" "$TMP/funcoes_switch.c"
$CC $CFLAGS "$TMP/funcoes_switch.c" -lm -o "$TMP/funcoes_switch"
"$TMP/funcoes_switch" > "$TMP/funcoes_switch.out"
grep -F 'soma=30' "$TMP/funcoes_switch.out"
grep -F 'resultado=30; calculo=10' "$TMP/funcoes_switch.out"

"$TMP/cbr" "$ROOT/programa.cbr" "$TMP/programa.c"
$CC $CFLAGS "$TMP/programa.c" -lm -o "$TMP/programa"
printf 'Freud\n12\n7.5\n' | "$TMP/programa" > "$TMP/programa.out"
grep -F 'Nome: Freud' "$TMP/programa.out"
grep -F 'Resultado: aprovado' "$TMP/programa.out"
grep -F 'For classico: 0 1 2' "$TMP/programa.out"

if "$TMP/cbr" "$ROOT/tests/erro_variavel.cbr" "$TMP/erro.c" > "$TMP/erro.out" 2>&1; then
    echo 'ERRO: tradutor aceitou variável não declarada' >&2
    exit 1
fi
grep -F 'variável usada na expressão não foi declarada' "$TMP/erro.out"

echo 'Todos os testes da Cbr 1.0 Estável passaram.'
