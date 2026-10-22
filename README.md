# Wii2GameCube — experimental PowerPC binary compatibility

Projeto independente de **Maicon Nascimento**, código MIT. É um experimento técnico; Wii/GC são marcas de seus respectivos titulares.

> Não executa títulos WiiWare, o Wii Menu, IOS ou homebrews Wii tradicionais vinculados à libogc no GameCube. Nenhum jogo ou firmware Nintendo é distribuído.

## Evidências coletadas no Dolphin Android

1. Teste isolado: `wii_guest_sample.dol` iniciou no **modo Wii** e exibiu vídeo.
2. Teste isolado: `gcwcl_probe.dol` iniciou no **modo GameCube** e mostrou heartbeat.
3. Fase 1: código PowerPC freestanding compilado para Wii executou na memória do GC; captura `PASS 5/5`.
4. **Fase 2: confirmação visual no Dolphin GameCube** do DOL Wii de 352 bytes, 96 bytes de código PPC carregados, `PASS 5/5`, com quadrado 2D visível. Isso valida o DOL mínimo e seu programa independente, **não** um homebrew Wii completo ou os serviços IOS.

## Fase 3: cenas 2D criadas pelo próprio guest Wii

O experimento da fase 2 calcula apenas coordenadas. O **guest scene** da fase 3 vai além: código compilado com flags Wii recebe um ponteiro EABI para um bloco de comandos de desenho, escreve 3 retângulos animados, suas cores e posições, e retorna uma assinatura. O host GameCube valida assinatura, versão, limites e sentinelas de memória, então traduz os comandos para pixels YUY2.

O GC mantém o renderizador e acesso ao hardware; o guest não depende da GPU, biblioteca Wii, IOS, Bluetooth ou MEM2. **Ainda é uma ABI própria de pesquisa**, não uma implementação das APIs Wii.

O guest original da fase 2 continua como fallback, se o gerador de cena falhar.

### Como usar no Dolphin

O teste é **opcional**, pois a partir de agora priorizamos validações automáticas: baixe o último artifact aprovado `wii2gamecube-phase3-guest-scene` e carregue `gamecube_probe/gcwcl_probe.dol` **em modo GameCube**. A tela deverá indicar `Wii->GC scene ABI: 5/5 PASS` e mostrar três sprites comandados pelo guest Wii.

A CI testa a ABI C em ambiente nativo, valida o código PPC gerado e os DOLs, compila os programas e publica o arquivo. Isso **não** substitui emulação dinâmica; a implantação de teste automático Dolphin segue como tarefa independente.

## Código-fonte

| Diretório/arquivo | Papel |
|---|---|
| `guest_probe/` | Função PowerPC Wii original da fase 2 |
| `guest_scene/` | ABI de cena 2D e programa Wii-freestanding que constrói comandos de desenho |
| `gamecube_probe/source/scene_runtime.c` | Validação ABI + tradução das cenas para XFB YUY2 |
| `gamecube_probe/` | Host GameCube que incorpora os dois DOLs, com fallback |
| `tools/guest_gate.py` | Gate de tamanho/retorno/relocações no guest |
| `tools/pack_guest_dol.py` | Empacotador DOL mínimo de seção única |
| `tests/test_guest_scene.py` | Testes nativos de contrato, movimento e limites |
| `.github/workflows/build.yml` | Compilação PowerPC + artefatos + testes |

## Executar testes locais

```bash
python3 -m unittest discover -s tests -v
make -C guest_scene
python3 tools/guest_gate.py guest_scene/scene.o guest_scene/scene.bin
python3 tools/dol_inspect.py guest_scene/scene.dol --strict
make -C gamecube_probe
```

Compilações Wii/GC requerem devkitPro, devkitPPC e libogc. Os testes Python usam somente o compilador C nativo disponível no Linux.

## Princípios

- Preservar exemplos que já funcionam; apenas adicionar novas etapas isoladas.
- Não abrir execução de DOLs arbitrários na memória GC.
- Sem suporte de controles nesta fase.
- Não inferir compatibilidade geral de Wii a partir da arquitetura PowerPC compartilhada.
- Medir avanços por demonstrações e validações, não por percentuais atribuídos a commits.

Veja [ROADMAP](docs/ROADMAP.md) e [TEST_MATRIX](docs/TEST_MATRIX.md).
