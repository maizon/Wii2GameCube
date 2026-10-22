# Plano verificável de pesquisa

Critério de sucesso geral: registrar no GameCube real (ou Dolphin GameCube, antes do hardware) a execução de pelo menos uma instrução controlada vinda de um binário de teste produzido na configuração Wii, sem recompilar esse binário para GameCube.

## Gate 0 — teste de infraestrutura
- [x] Analisador de cabeçalho DOL + testes sintéticos.
- [x] Fonte de diagnóstico GameCube e guest mínimo Wii.
- [ ] Compilação PowerPC em CI confirmada.
- [ ] Diagnóstico GameCube inicia no hardware ou Dolphin GC.
- [ ] Guest de Wii inicia no Wii ou Dolphin Wii.

## Gate 1 — prova de PPC compartilhado
- [ ] Especificar ABI para guest freestanding (sem IOS e sem libogc).
- [ ] Produzir binário determinístico de teste com script de link e endereços explícitos.
- [ ] Projetar trampoline numa região reservada/isolada do loader (proteger stack, IRQ e framebuffer).
- [ ] Validar campos DOL, segmentos, alinhamento, entry, sobreposição e cache antes da execução.
- [ ] Retornar valor de um guest PPC em GC; registrar execução e retorno, sem acesso a MMIO Wii.
- [ ] Comparar instruções assembly e comportamentos; documentar prova.

## Gate 2 — Wii userland mínimo
- [ ] Identificar rotinas chamadas pelo guest Wii de teste; identificar requisitos reais.
- [ ] Projetar camada de vídeo 2D adaptada apenas às APIs observadas.
- [ ] Investigar chamadas IOS com base em traços, não prometer emulação completa.
- [ ] Demonstrar animação 2D Wii de autoria própria no GC.

## Gate 3 — escopo expandido
- [ ] Gerenciador de assets e orçamento RAM.
- [ ] Testes de GC ARAM e SD, latência/throughput medidos.
- [ ] Avaliar WiiWare 2D apenas se houver evidência técnica e direitos para análise.
- [ ] Wii Menu original: fora de escopo até termos resultados concretos de Gates 1–2.

Sem mecanismos para contornar proteção, obter chaves, distribuir ISOs, WADs, NAND ou conteúdo protegido.
