# Wii2GameCube — marcos e evidências

## Gate 0: toolchain e homebrew independente

- [x] Wii e GameCube compilam com devkitPPC e CI verde.
- [x] Capturas de execução separada em Dolphin Wii e GC.

## Gate 1: PowerPC guest embutido

- [x] Código freestanding produzido com `wii_rules`.
- [x] Execução cruzada no Dolphin GameCube, captura `PASS 5/5`.

## Gate 2: DOL Wii mínimo com vídeo

- [x] Empacotador + inspetor estrito de DOL de seção única.
- [x] Carregamento e realocação do DOL controlado em MEM1.
- [x] Cache/ABI/memória; execução de cinco vetores de teste.
- [x] **Captura GameCube fornecida:** `Guest Wii DOL: 352 bytes`, `PASS 5/5`, quadrado branco visível.
- [ ] Automatizar a emulação dinâmica no CI sem usuário.

## Gate 3: guest Wii comandando animação 2D

- [x] ABI de comandos de cena versionada, sem endereços brutos de hardware.
- [x] Programa freestanding Wii produz 3 retângulos de cena e assinatura.
- [x] Host GC executa guest, valida estrutura, limites e sentinelas; renderiza YUY2.
- [x] Fallback para o sprite legado da fase 2 em caso de falha.
- [x] Testes C nativos da mesma fonte, independentemente do binário GC.
- [ ] CI para novo guest PPC compilado e DOL, artifact confirmado.
- [ ] Emulação GC automática confirma resultado 5/5 e múltiplos sprites.

## Gate 4: compatibilidade de um homebrew Wii completo

- [ ] Testar um homebrew Wii open-source real com stack/dados/BSS e chamadas de sistema reais.
- [ ] Abordagem para IOS/video compatível no GC (não apenas uma ABI criada para o projeto).
- [ ] Testes de MEM1/ARAM/SD, com medições reais e testes no hardware GC.
- [ ] Sem promessa de WiiWare ou Wii Menu antes de demonstração de dependências resolvidas.

Por decisão do projeto, **não exigimos testes manuais recorrentes do usuário**. O teste visual prévio é evidência da fase 2. Progresso geral estimado não é porcentagem de linhas nem commits; metas experimentais são incertas.
