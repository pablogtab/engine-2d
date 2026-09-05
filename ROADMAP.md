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
4. Reducir el alcance de las recompilaciones moviendo implementación pesada de
   headers compartidos a `.cpp`, y separar el build del editor en `dev.sh` de la
   compilación explícita de tests. Evitar compilar simultáneamente el mismo
   cambio en el build habitual y en otro directorio de validación.

## Ergonomía de EngineScript

- [ ] Incorporar transferencia explícita de estado seleccionado entre escenas
  y durante hot reload. Sólo participará estado serializable; la restauración
  validará tipo y versión, aplicará una migración compatible o reiniciará de
  forma segura con diagnóstico. No se copiarán punteros, handles nativos ni
  entidades completas.
- [ ] Diseñar por separado una futura persistencia de entidades equivalente a
  `DontDestroyOnLoad`; no debe quedar implícita en los campos `static` ni en la
  transferencia de estado.
- [ ] Mantener una sola documentación vigente de EngineScript. Cada cambio del
  lenguaje debe actualizar contratos, ejemplos, LSP y datos machine-readable;
  Git conserva el historial y no se publican documentos separados por versión.

## Renderer y presentación

- [ ] Diseñar `MaterialInstance`/property blocks para sobrescribir parámetros
  por renderer sin duplicar shader ni modificar el material compartido.
  - Definir identidad, herencia del material base y ciclo de vida de overrides.
  - Agrupar instancias con valores compatibles y medir el impacto real sobre
    batching y draw calls antes de exponer setters dinámicos a EngineScript.
  - Incorporar parámetros tipados/reflejados, validación de hot reload,
    serialización sólo cuando sean datos de autoría y preview/Undo del editor.
  - Conectar esos overrides al VFX Preview para editar seeds del shader y
    comparar cuatro variaciones deterministas del mismo efecto en paralelo.
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
[`docs/RENDER_MATERIALS.md`](docs/RENDER_MATERIALS.md). El preview aislado de
prefabs visuales está documentado en
[`docs/VFX_PREVIEW.md`](docs/VFX_PREVIEW.md).

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

- [ ] Establecer una escena/recorrido reproducible para presupuestos de 16,67 ms,
  registrando energía, resolución física, carga y configuración de compilación;
  comparar editor Debug, player Release y hardware mínimo real con P95/P99.
- [ ] Reducir la sincronización de campos expuestos y las lecturas de assets por
  callback de scripts, conservando orden, actualización de componentes y hot
  reload. Medir cada cambio con el mismo recorrido antes de ampliar su alcance.
- [ ] Definir perfiles de calidad medidos para iluminación, partículas y
  resolución, manteniendo la misma simulación en equipos modestos.

- [ ] Completar las APIs de autoría asistida pendientes: comandos/build
  auditables, previsualización de diffs de archivos, recibos estructurados,
  audio, materiales, prefabs, escenas, Tilemap, animación y capturas de juego.
  El contrato actual está en
  [`docs/AI_ASSISTED_AUTHORING.md`](docs/AI_ASSISTED_AUTHORING.md) y
  [`docs/PROJECT_COMMANDS.md`](docs/PROJECT_COMMANDS.md).
- [ ] Ejecutar y confirmar en GitHub el workflow `engine-integrity.yml` añadido
  para Debug/Release, UBSan, CTest y smoke de exportación macOS. La validación
  local y la resolución de F01–F27 se documentan en
  [`CORRECCIONES.md`](docs/audits/2026-09-04/CORRECCIONES.md).
- [ ] Ampliar CI con formato, análisis estático, sanitizadores de memoria/hilos
  y pruebas en Windows/Linux; las comprobaciones de GPU requieren hardware.
- [ ] Medir sesiones prolongadas y presupuestos de frame/memoria con un juego
  representativo antes de declarar preparación productiva general.

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
