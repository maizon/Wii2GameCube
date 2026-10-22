# Wii2GameCube — pesquisa de compatibilidade Wii → GameCube

Experimentos PowerPC desenvolvidos por **Maicon Nascimento**. Código original MIT; não relacionado à Nintendo.

> **Atenção:** O projeto **não executa jogos WiiWare, Wii Menu ou homebrews Wii completos no GameCube**. A etapa atual investiga uma ABI comum através de um DOL mínimo, sem serviços IOS. Não distribuir ISOs, NAND, chaves, WADs ou jogos.

## Fatos demonstrados

- ✅ Homebrew de Wii compilado com devkitPPC e iniciado no **Dolphin modo Wii**.
- ✅ Programa diagnóstico GameCube compilado e iniciado no **Dolphin modo GC**.
- ✅ Fase 1: captura real no Dolphin GC recebida, mostrando `PASS Wii-target PPC execution: 5/5`. Uma função isolada PPC compilada com flags de Wii foi copiada e executada em GC. Isso **não prova** compatibilidade com bibliotecas Wii.

## Nova fase 2 (implementada no repositório)

O `guest_probe/` compila uma função freestanding usando as flags de Wii, extrai seu código e gera um arquivo DOL mínimo com uma seção de texto e entrada `0x80018000`.

O `gamecube_probe/` agora incorpora **esse DOL**, valida seu cabeçalho e move sua seção de código (80 bytes úteis, preenchida até 96 bytes por alinhamento) para memória MEM1 alocada. Atualiza caches, executa a função no GameCube e apresenta uma animação 2D (quadrado móvel) diretamente no framebuffer YUY2, cujas coordenadas vêm do código compilado para Wii.

O resultado ainda é experimental, pois a nova execução no Dolphin GC não foi testada dinamicamente em CI.

### Por que não carregar o Wii `.dol` completo?

Homebrews comuns são ligados a libogc, driver de vídeo Wii, MEM2, IOS e/ou dependências ausentes no GC. Nosso único guest executável no GC usa código original controlado **sem nenhum acesso a recursos exclusivos do Wii**. Isso não é emulador de IOS, nem um loader universal.

### Estrutura

| Arquivo | Uso |
|---|---|
| `guest_probe/source/guest.c` | Função PPC compilada com configuração Wii |
| `tools/pack_guest_dol.py` | Serializa/valida o DOL freestanding de seção única |
| `tools/guest_gate.py` | Valida instrução de retorno, tamanho e ausência de relocação |
| `gamecube_probe/source/main.c` | Valida e executa DOL embutido no host GC; renderização 2D |
| `wii_sample/source/main.c` | Aplicação Wii tradicional usada como referência |
| `tools/dol_inspect.py` | Analisa estrutura de DOL sem afirmar compatibilidade |
| `tests/` | Testes sintéticos e de segurança |
| `.github/workflows/build.yml` | Compila, valida e publica artefatos |

### Compilar e testar

Ferramentas: devkitPro / devkitPPC / libogc / libfat, além de Python 3.

```bash
python3 -m unittest discover -s tests -v
make -C guest_probe
python3 tools/guest_gate.py guest_probe/guest.o guest_probe/guest.bin
python3 tools/dol_inspect.py guest_probe/guest.dol --strict
make -C gamecube_probe
make -C wii_sample
```

**A validação automatizada é pré-requisito para merge, mas não é substituto para execução do software sob emulação GameCube.** A próxima meta é instrumentar Dolphin no CI. Não são necessários controles no estágio atual.

Leia a [matriz de evidências](docs/TEST_MATRIX.md) e [roadmap](docs/ROADMAP.md).
