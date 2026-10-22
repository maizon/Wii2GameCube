# Roteiro medido por evidências

## Gate 0 — builds e execução separada

- [x] Analisador DOL, verificações sintéticas.
- [x] CI compilou os dois DOLs nativos (Wii e GameCube).
- [x] Usuário enviou screenshot do guest **no modo Wii** (tela de texto).
- [x] Usuário enviou screenshot do programa GC **no modo GameCube** (heartbeat).
- [ ] Execução confirmada em GameCube físico (ainda apenas Dolphin).

## Gate 1 — código PowerPC originalmente produzido com flags Wii no GameCube

- [x] Criar um guest freestanding, fonte aberta, sem dependências de IOS/libogc.
- [x] Compilar Wii PowerPC com `-mrvl`, extrair somente seção `.text.guest`.
- [x] Projetar chamada EABI que retorna `uint32_t` com um argumento `uint32_t`.
- [x] Embutir os bytes Wii como dados no programa GC.
- [x] Copiar para um buffer alinhado na arena GC, sincronizar caches e executar por função-ponte.
- [x] Validar tamanho, alinhamento e rejeitar relocations no código guest em CI.
- [ ] Verificar build fase 1 e integridade do artifact com logs GitHub.
- [ ] **Validar em Dolphin modo GameCube**: execução correta em cinco vetores e coordenadas animadas.
- [ ] Verificar GameCube físico, memória e timing.

Este primeiro guest NÃO é um binário DOL completo de Wii. A cópia de código freestanding não é prova de compatibilidade com o restante do sistema Wii.

## Gate 2 — um DOL Wii mínimo, com ABI restrita

- [ ] Compilar DOL Wii freestanding com apenas uma seção e entry explicitamente acordado.
- [ ] Criar loader restrito que parseie header, valide limites, *não* carregue código arbitrário.
- [ ] Definir modelo de relocação PIC ou transformação segura de endereços.
- [ ] Validar endianness, calling convention, stack, relocations, dcache/icache.
- [ ] Tentar execução Wii DOL de autoria própria no GC sem recompilá-lo para GC.
- [ ] Só então expandir para um sprite 2D desenhado na tela.

## Gate 3 — avaliação de software Wii real

- [ ] Instrumentar dependências reais de libogc, IOS, MEM2, vídeo, áudio.
- [ ] Investigar ARAM/SD para recursos; medir latência real.
- [ ] Selecionar um homebrew 2D com código disponível e licença compatível.
- [ ] WiiWare 2D, Wii Menu: apenas se houver provas e permissões; metas de longo prazo.

**Sem promessa de progresso em percentuais arbitrários**: cada gate tem critérios de aceitação observáveis. Controles são adiados conforme escolha do projeto.
