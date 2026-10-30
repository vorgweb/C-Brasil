# Cbr 1.0 — versão estável

Cbr é uma linguagem de sintaxe simplificada traduzida para C. O tradutor gera código C automaticamente.

## Compilar

```sh
gcc -std=c11 -Wall -Wextra -Werror -O2 cbr.c -lm -o cbr
```

## Traduzir e executar o exemplo integrado

```sh
./cbr-linux programa.cbr
gcc -std=c11 -Wall -Wextra -Werror -O2 programa_gerado.c -lm -o programa
./programa
```

O exemplo pede nome, idade e nota e demonstra recursos da linguagem.

## Recursos desta versão

- Tipos: `str`, `int`, `float`, `bool`.
- Atribuição e reatribuição com verificação de tipo.
- Expressões numéricas com precedência, parênteses, `%`, `**`, comparações e operadores booleanos `&&`, `||`, `!`.
- `printf()` com interpolação tipada: `{str.nome}`, `{int.idade}`, `{float.nota}`, `{bool.aprovado}`.
- `scan()` para entrada de texto, inteiro, decimal e booleano.
- `if/else`, `while`, `for nome in range(fim)`, `for nome in range(inicio, fim)`, `for` clássico, `break` e `continue`.
- `switch`, `case`, `default` e `break`.
- Funções de usuário com até oito parâmetros numéricos e `return` numérico.
- Arrays heterogêneos com capacidade de 256 elementos, leitura/alteração por índice, `tamanho`, `adicionar`, `remover` e `limpar`.
- Biblioteca de texto: `juntar`, `tamanho`, `maiusculas`, `minusculas`, `contem`, `subtexto`, `substituir`.
- Biblioteca matemática: `raiz`, `absoluto`, `arredondar`, `piso`, `teto`, `seno`, `cosseno`, `tangente`, `logaritmo`, `log10`, `exponencial`, `maximo`, `minimo`, `potencia` e `limitar`.
- Mensagens de erro com arquivo e linha, validação de identificadores, expressões, argumentos e limites de arrays.
- Opções de terminal `--help` e `--version`.

## Limitações conhecidas

`import`, `try/catch`, dicionários, depurador interativo, parâmetros e retornos de texto/booleano em funções de usuário, arrays expansíveis acima de 256 elementos, indexação de arrays em expressões matemáticas e comparações encadeadas não fazem parte desta versão; ficam para versões futuras. `tamanho()` conta bytes, e a conversão de maiúsculas/minúsculas é voltada a ASCII.

## Testes

Execute `./testes.sh` para compilar o tradutor, traduzir e compilar os exemplos e verificar resultados esperados.
