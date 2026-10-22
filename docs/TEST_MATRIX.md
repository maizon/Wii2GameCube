# Evidências e checklist de validação

## Fase 2 — comprovada por screenshots de Dolphin GameCube

Captura 21/10/2026:
- `Guest Wii DOL: 352 bytes | TEXT 0x80018000 rebased to MEM1`
- `Wii PPC: loaded 96 code bytes into MEM1`
- `PASS Wii-target PPC execution: 5/5`
- `2D guest sprite: small moving square in YUY2 XFB`
- Retângulo inferior com sprite claro, renderizado no framebuffer.

O outro screenshot confirma somente `wii_guest_sample.dol` no Dolphin Wii.

**Interpretação precisa:** o host GC executa uma função PPC original, empacotada em DOL mínimo de Wii. Não foi demonstrada execução de código Nintendo, IOS, MEM2 ou bibliotecas Wii.

## Fase 3 — critérios automáticos

- Testar a função `wii_guest_scene` da **mesma fonte** compilada nativamente com `cc`.
- Verificar o tamanho de `Wii2GcScene` (96 bytes) e os campos 32 bits da ABI.
- Testar chamadas determinísticas para frames 0, 1, 17, 123, 65535, UINT32_MAX.
- Verificar três sprites com coordenadas/dimensões dentro de 0..1000.
- Compilar código Wii PowerPC com `-mrvl`, sem relocations em `.text.guest`.
- Validar integridade e empacotamento determinístico do DOL.
- Construir host GC com dois DOLs embutidos e fallback da fase 2.

## Fase 3 — dinâmica no Dolphin GC (ainda falta automação)

A nova tela deve conter:
```
Scene Wii DOL: ...
Wii->GC scene ABI: 5/5 PASS
Phase 3: Wii guest generates 3 rects/frame
```

O vídeo deverá exibir três sprites animados na região inferior, em lugar do sprite único da fase 2. Não confundir build verde com execução real: o teste automático do emulador continua pendente.
