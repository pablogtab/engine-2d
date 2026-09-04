# Engine2D — Hoja de ruta activa

Este archivo contiene sólo trabajo pendiente. Los hitos completados, planes de
implementación cerrados y migraciones históricas se eliminan en favor de los
contratos vigentes en `docs/` y de la historia Git.

## Prioridad inmediata

1. Validar visualmente el proyecto de referencia tras las nuevas utilidades de
   EngineScript: órbita del pet, Noise y helpers de Tween.
2. Convertir `Tweens` game-side de helpers puros a un runner actualizable por
   frame, con handles cancelables y ciclo de vida propiedad del juego.
3. Incorporar fuente exacta y filtros estructurados para fallos de callbacks
   nativos de EngineScript.

## Ergonomía de EngineScript

- [ ] Ejecutar por incrementos el plan de seguridad y ergonomía del lenguaje:
  vistas de componentes estables, interpolación tipada, bindings opcionales,
  `match`, argumentos por defecto y nombrados, colecciones apropiadas para
  gameplay y auditoría estructurada para IA. Ver
  [`docs/ENGINESCRIPT_ERGONOMICS_PLAN.md`](docs/ENGINESCRIPT_ERGONOMICS_PLAN.md).
- [ ] Mantener una sola documentación vigente de EngineScript. Cada cambio del
  lenguaje debe actualizar contratos, ejemplos, LSP y datos machine-readable;
  Git conserva el historial y no se publican documentos separados por versión.

## Renderer y presentación

- [ ] Establecer un pipeline de color lineal de extremo a extremo.
  - El contrato actual usa canales sRGB `SDL_Color` para sprites, luces,
    render targets y postproceso. `Color.srgbToLinear` y
    `Color.linearToSrgb` son helpers explícitos, no un cambio de pipeline.
  - Definir decode de texturas, blend e iluminación lineales, formatos de
    render target, presentación sRGB y validación mediante imágenes de
    referencia en cada backend soportado.
- [ ] Crear Shader Graph versionado: puertos tipados, canvas, validación,
  generación determinista de HLSL, caché, preview y smoke tests por backend.
- [ ] Completar controles de editor pendientes: búsqueda/favoritos de
  componentes, reversibilidad de propiedades visuales y el conjunto de pruebas
  de geometría, gizmos y viewport.

El contrato vigente de composición, Canvas y materiales está en
[`docs/RENDERER_CANVAS_CONTRACTS.md`](docs/RENDERER_CANVAS_CONTRACTS.md) y
[`docs/RENDER_MATERIALS.md`](docs/RENDER_MATERIALS.md).

## Assets, runtime y distribución

- [ ] Migrar el registro de assets a un `AssetRegistry.json` versionado y mover
  índices, previews, estado de importación y datos reconstruibles a
  `Library/AssetIndex`; realizar una conversión única del proyecto de referencia.
- [ ] Separar la base mutable del editor del resolver de runtime y generar un
  `RuntimeAssetManifest` inmutable durante export.
- [ ] Incorporar un cooker de escenas y prefabs para builds distribuidos.
  - Mantener JSON como fuente de autoría editable y generar artefactos binarios
    versionados, deterministas y validados durante export.
  - Resolver referencias por `AssetHandle`, eliminar metadatos exclusivos del
    editor y rechazar estado `runtime.transient` o campos privados de ejecución.
  - Empaquetar los artefactos cocinados con compresión, hashes de integridad y
    compatibilidad explícita de versión; el runtime distribuido no debe depender
    de los `.scene` y `.prefab` de autoría.
  - Medir tamaño y tiempo de carga frente al JSON actual y cubrir arranque y
    cambio de escena mediante smoke tests del paquete final.
- [ ] Añadir atlas estáticos como etapa derivada para texturas importadas
  compatibles de color, normal y emisión: guardar páginas en `Library/Atlases`,
  invalidarlas por fuentes o ajustes, conservar escenas y assets de autoría, y
  excluir texturas runtime/procedurales hasta contar con un asignador dinámico.
- [ ] Empaquetar y probar en máquinas limpias macOS, Windows y Linux; incluir
  layouts nativos, dependencias, firma donde corresponda y smoke tests sin
  árbol `Assets/` de autoría.
- [ ] Añadir targets y smoke tests reales para iOS y Android antes de declarar
  soporte móvil.

## Diagnóstico, rendimiento y automatización

- [ ] Ejecutar el plan de diagnóstico y rendimiento por incrementos medibles:
  diagnósticos estructurados, overlays, inspección de runtime, profiler y
  presupuestos de estrés. Ver
  [`docs/DEBUGGING_PERFORMANCE_PLAN.md`](docs/DEBUGGING_PERFORMANCE_PLAN.md).
- [ ] Completar las APIs de autoría asistida pendientes: comandos/build
  auditables, previsualización de diffs de archivos, recibos estructurados,
  audio, materiales, prefabs, escenas, Tilemap, animación y capturas de juego.
  El contrato actual está en
  [`docs/AI_ASSISTED_AUTHORING.md`](docs/AI_ASSISTED_AUTHORING.md) y
  [`docs/PROJECT_COMMANDS.md`](docs/PROJECT_COMMANDS.md).
- [ ] Incorporar CI para formato, análisis estático, build, pruebas y smoke
  tests aplicables por plataforma.

## Criterios permanentes

- No declarar una característica de renderer, export o plataforma como lista
  sin validación en hardware/entorno representativo.
- Cada cambio serializable conserva registro, round-trip, snapshot, validación
  y Undo/Redo cuando aplique.
- Play, Pause y previews sólo modifican estado transitorio en memoria: ninguna
  ruta de Save, autosave, hot-exit o export puede serializar ese estado en una
  escena o prefab de autoría.
- Cada corrección incorpora una regresión en la capa más baja posible.
- Antes de entregar: build y suites aplicables, `e2sc --check` para cambios de
  EngineScript, y `git diff --check`.
