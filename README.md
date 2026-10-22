# Wii2GameCube — Compatibilidade PowerPC experimental

**Estado: fase 1 em desenvolvimento — executar uma função PPC construída com o alvo Wii no processo de um programa GameCube.** Nenhuma compatibilidade genérica com Wii foi demonstrada.

Projeto experimental de Maicon Nascimento. Wii e GameCube usam PowerPC, mas Wii possui hardware, IOS, MEM2 e APIs que **não existem no GameCube**. Este repositório testa o que pode ser executado nativamente sem fingir que um binário comercial funcionará automaticamente.

## Resultados confirmados

As capturas do primeiro teste no Dolphin mostraram:
- `wii_guest_sample.dol` rodando no **modo Wii**, inicializando vídeo;
- `gcwcl_probe.dol` fase 0 rodando no **modo GameCube**, mostrando heartbeat.

São duas execuções independentes: isso não demonstra um Wii DOL rodando no GameCube.

## Fase 1: nova demonstração (sem SD e sem controles)

A construção do GameCube inclui um **guest PPC freestanding** compilado com `wii_rules` / `-mrvl`. Não é o executável completo de um homebrew de Wii.

O código do guest recebe o número de um frame, computa coordenadas 2D (x,y) e retorna. O código Wii é extraído da seção `.text.guest` e embutido como bytes, copiado pelo GC para memória própria alinhada em 32 bytes. O host sincroniza D-cache/I-cache, chama a rotina, confere cinco casos de teste e mostra as coordenadas.

A rotina não faz chamadas ao IOS, ao sistema, à GPU ou à memória MEM2. A interface de retorno é explícita e restrita; por segurança, nenhum DOL arbitrário é executado.

### Teste no Dolphin Android (modo GameCube)

1. Vá à aba Actions, abra a **última execução verde** na branch `feat/wii-ppc-guest-execution` ou `main` após merge.
2. Baixe o artifact `wii2gamecube-phase1-ppc-proof`.
3. Extraia e abra **`gamecube_probe/gcwcl_probe.dol` no Dolphin GameCube** (identifique os botões do GameCube no overlay, não Wii).
4. Procure `PASS Wii-target PPC execution: 5/5` e mudanças periódicas nas coordenadas `sprite(x,y)`.
5. Se falhar, envie captura de tela e o log do Dolphin; não precisa de SD nem de controle.

Veja [TEST_MATRIX.md](docs/TEST_MATRIX.md) para resultados esperados e limitações.

## Árvore do projeto

- `guest_probe/`: rotina Wii-freestanding, código PPC sem sistema operacional; seu Makefile gera `guest.bin`.
- `gamecube_probe/`: aplicativo GameCube que testa dinamicamente os bytes do guest construído para Wii, em MEM1; `sd:/guest.dol`, quando presente, tem **apenas seu cabeçalho inspecionado**, não é executado.
- `wii_sample/`: homebrew Wii original, usado como controle de referência em Dolphin Wii.
- `tools/guest_gate.py`: rejeita código de guest sem `blr`, maior que 4 KiB ou com relocação em `.text.guest`.
- `tools/dol_inspect.py`: inspeção estrutural de DOL, sem declarar compatibilidade.
- `tests/`: testes Python, incluindo verificações negativas.

## Compilar

Use devkitPro + devkitPPC + libogc + libfat, como no GitHub Actions:

```bash
make -C guest_probe
python3 tools/guest_gate.py guest_probe/guest.o guest_probe/guest.bin
make -C gamecube_probe
make -C wii_sample
python3 -m unittest discover -s tests -v
```

DOLs compilados não contêm jogos da Nintendo. O projeto não fornece ISOs, WADs, NAND, firmware nem chaves de terceiros. Apenas código original MIT.

## Marcos

| Marco | Estado |
|---|---|
| Build dos 2 programas separados Wii/GC | ✅ CI fase 0 |
| Teste Dolphin Wii/GC separados | ✅ Capturas fornecidas |
| Função PPC Wii-freestanding embutida no host GC | ✅ Código em desenvolvimento; execução a validar |
| Execução da rotina Wii-target no Dolphin GC | ⏳ A validar com novo DOL |
| Carregamento de homebrew Wii DOL completo no GC | ❌ Não implementado |
| WiiWare 2D, IOS, Wii Menu original | ❌ Não suportados |

Consulte [ROADMAP.md](docs/ROADMAP.md). *Nintendo, Wii e GameCube são marcas dos respectivos titulares; não há afiliação.*
