# VFX Preview

`VFX Preview` ejecuta un prefab visual en una escena temporal que no modifica
la escena de autoría. Se abre desde el menú contextual de un prefab en el Asset
Browser mediante `Open in VFX Preview`.

El preview incluye la jerarquía completa del prefab y actualiza EngineScript,
sprites animados, materiales, emisores de partículas y trails. Las APIs de
prefabs y partículas de EngineScript se redirigen a ese mundo temporal. Audio,
input, física y cambios de escena quedan fuera del preview para evitar efectos
laterales sobre el proyecto abierto.

## Controles

- Play/Pause, Restart y avance fijo de un frame a 60 Hz.
- Loop, velocidad entre `0.1x` y `2.0x` y duración editable.
- Timeline con reconstrucción y simulación determinista al soltar el cursor.
- Seed reproducible para partículas y generación de una nueva seed.
- Color de fondo, encuadre automático, zoom con rueda y paneo con botón central
  o `Space + arrastre izquierdo`.
- Panel lateral para editar campos `@expose` de EngineScript y propiedades
  reflejadas de los materiales durante la simulación.

Los cambios aparecen como `UNSAVED` y se conservan al reiniciar o mover la
timeline. `Save` escribe los valores públicos de EngineScript en el prefab y
las propiedades materiales en cada asset `.material`; no modifica el código
fuente del script ni el shader. `Reset` descarta los cambios sin guardar y
recarga los valores desde los assets. Las referencias a entidades y assets se
muestran como solo lectura porque pertenecen al mundo aislado.

El reloj que reciben los shaders es propio del preview. Pausar congela tanto
EngineScript como `engine_time`; mover la timeline reconstruye el prefab y lo
simula en pasos fijos hasta el instante solicitado.

## Aislamiento de Play Mode

El runtime temporal se destruye al cerrar u ocultar el panel. También se
suspende durante el arranque y la ejecución de Play Mode, antes de que comiencen
los scripts de la escena principal, para que ambos runtimes no compartan estado
estático de un módulo EngineScript. Al volver a Edit Mode el preview se
reconstruye desde el asset.

## Alcance pendiente

El seed actual controla partículas. Los parámetros materiales se implementan
mediante clones privados del preview, por lo que todos los renderers que usan el
mismo material comparten el ajuste temporal. Seeds y overrides independientes
por renderer y comparación simultánea de variaciones dependen del futuro
contrato `MaterialInstance`/property blocks.
