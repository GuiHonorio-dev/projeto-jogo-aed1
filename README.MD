# JOGO AED1 — Zuma

Recriação do Zuma (PopCap) em C++17 com SFML 3.

## Compilar e jogar

```sh
make run
```

Rode sempre a partir da pasta do projeto, porque os recursos (`sprites/`, `fontes/`) são carregados por caminho relativo.

## Controles

| Ação | Tecla |
|------|-------|
| Mirar | Mouse |
| Atirar | Clique esquerdo |
| Trocar a bola da boca pela das costas | Clique direito ou Espaço |
| Pausar | P ou Esc |

## Regras

- Junte 3 ou mais bolas da mesma cor para destruí-las.
- Quando as pontas de um buraco na corrente têm a mesma cor, a parte da frente volta atraída e pode gerar **combos**.
- Encha a barra do **Zuma** para parar de sair bolas e destrua o resto da corrente para passar de fase.
- Se uma bola chegar ao buraco, você perde uma vida.
- Bolas brilhando trazem poderes: **bomba**, **ré**, **devagar** e **precisão**.

## Arquivos

- `main.cpp`: o jogo
- `estruturas.hpp`: estruturas de dados implementadas à mão (sem `std::list`/`queue`/`stack`)
- `sprites/caminho.txt`: pontos do trilho, extraídos do `mapa.png`
- `fontes/DejaVuSans-Bold.ttf`: fonte do placar

## Estruturas de dados

| Estrutura | Onde é usada no jogo |
|-----------|----------------------|
| `Lista<T>` (duplamente encadeada) | A **corrente de bolas** no trilho (`Game::balls`). Atirar insere um nó no meio (`inserirAntes`/`inserirDepois`), combinar 3+ remove um trecho (`remover`) e novas bolas entram no começo (`inserirInicio`). `anterior`/`proximo` dão os vizinhos usados para achar combinações. |
| `Fila<T>` | A **munição do sapo** (`Game::ammo`, 3 bolas, mostrada no canto inferior esquerdo). Atirar = `desenfileirar` e a bola nova entra com `enfileirar`. Trocar bola = desenfileirar e enfileirar a mesma (vai para o fim). |
| `Pilha<T>` | A **reação em cadeia das bombas** em `Game::removeBalls`: cada bola removida é empilhada e, ao desempilhar, se for bomba, empilha as vizinhas atingidas. |
