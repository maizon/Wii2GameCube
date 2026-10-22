# Roadmap técnico por demonstrações reprodutíveis

## Gate 0 — ambiente nativo
- [x] CI devkitPPC, Wii/GC compilam sem erros.
- [x] Capturas dos executáveis separados Wii/GC no Dolphin.

## Gate 1 — execução cruzada PPC
- [x] Guest de código isolado gerado com flags Wii (sem libogc/IOS).
- [x] Cache e EABI, execução de 80 bytes de PPC no GC.
- [x] **Demonstração visual no Dolphin GC:** captura 5/5 PASS enviada pelo usuário.

## Gate 2 — arquivo Wii DOL de seção única (em desenvolvimento)
- [x] Gerador DOL mínimo original + parser estrito em Python.
- [x] Loader GameCube restrito a DOL embutido, alocado em MEM1.
- [x] Gráficos 2D mínimos com sprite dirigido por guest PPC em YUY2.
- [x] Testes de regressão, negativos e verificação determinística na compilação.
- [ ] CI GitHub totalmente verde.
- [ ] Teste dinâmico de DOL embutido em emulação GameCube automatizada.
- [ ] Isolar ABI maior, dados e stack para programas Wii um pouco mais complexos.

## Gate 3 — aplicações Wii completas
- [ ] Loader relocável com dados/BSS e API de vídeo estritamente definida.
- [ ] Substituição de serviços mínimos para um homebrew open-source escolhido.
- [ ] Medição de custo de vídeo/CPU/memória/SD.
- [ ] Executar de fato um **homebrew Wii 2D completo** no GC e no Dolphin.

## Gate 4 — WiiWare/Wii Menu
- [ ] Avaliar requisitos reais de IOS, Hollywood, MEM2 e storage.
- [ ] Não supor que memória SD expande RAM de maneira transparente.
- [ ] WiiWare e Wii Menu continuam **fora do escopo de compatibilidade demonstrada**.

O progresso é medido por testes de aceitação, não por quantidade/percentual artificial de commits. Não dependemos de repetir testes manuais do usuário.
