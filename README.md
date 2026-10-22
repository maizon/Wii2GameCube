# Wii2GameCube — pesquisa de compatibilidade PowerPC

> **Estado: fase experimental 0. Ainda não executamos nenhum homebrew de Wii no GameCube.**

Projeto independente por Maicon Nascimento para investigar o aproveitamento de código PowerPC de homebrews de Wii em hardware GameCube, inicialmente **sem suporte a controles**.

## Objetivo verificável

Primeiro demonstrar que uma pequena rotina PowerPC compilada para um alvo Wii pode ser carregada em ambiente GameCube controlado, com validação de endereços, alinhamento, ABI, interrupções e cache. Depois investigar um aplicativo 2D simples. WiiWare comercial e o Wii Menu não são metas de curto prazo.

## Componentes

- `tools/dol_inspect.py`: inspeciona estrutura de arquivos DOL, regiões MEM1/MEM2, BSS, sobreposições e entrada; **não** é verificador de compatibilidade.
- `gamecube_probe/`: aplicativo nativo GameCube de diagnóstico de vídeo e leitura **somente do cabeçalho** de um `guest.dol` opcional no SD.
- `wii_sample/`: aplicativo de teste Wii simples e aberto, ainda não executado no GameCube.
- `tests/`: testes automatizados de análise estática.
- `.github/workflows/build.yml`: testes Python e builds PowerPC com devkitPro.

## Testes de PC

```bash
python3 -m unittest discover -s tests -v
python3 tools/dol_inspect.py path/to/your-own-homebrew.dol --json
```

## Build nativo

Requer devkitPro + devkitPPC + libogc + libfat. Para compilar com o ambiente configurado:

```bash
make -C gamecube_probe
make -C wii_sample
```

Artefatos esperados **quando o CI confirmar o build**: `gamecube_probe/gcwcl_probe.dol` (GameCube) e `wii_sample/wii_guest_sample.dol` (Wii).

O DOL de diagnóstico do GameCube **não carrega nem executa** um programa Wii. O fato de ambos usarem PowerPC não significa compatibilidade de serviços IOS, hardware, memória ou gráficos.

## Estado

| Marco | Status |
|---|---|
| Inspetor DOL e testes sintéticos | Implementado |
| Fontes do diagnóstico GC e exemplo Wii | Implementadas |
| Build PowerPC em CI | A verificar no GitHub Actions |
| Execução física em GameCube | Não realizada |
| Execução de guest Wii no GameCube | Não implementada |
| Compatibilidade com WiiWare ou Wii Menu | Não demonstrada |

Consulte `docs/ROADMAP.md` para critérios de aceite e limites. Este projeto não distribui ISOs, WADs, NAND, firmware, imagens de discos ou chaves proprietárias.

Licença MIT para código **original deste projeto**; Nintendo, Wii e GameCube são marcas de seus titulares. Projeto sem vínculo com a Nintendo.
