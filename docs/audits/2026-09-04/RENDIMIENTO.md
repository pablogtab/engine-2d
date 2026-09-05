# Seguimiento de la caída de FPS — 2026-09-05

El usuario reportó pasar de 60 FPS sostenidos a unos 30 FPS durante Play. El análisis combina tres capturas exportadas por el editor y muestreos del proceso real. Los tiempos incluyen ejecución Debug; no son mediciones GPU ni una comparación de escenas idénticas.

## Capturas anteriores al ajuste de partículas

| Captura | Frames en simulación | Entidades | Scripts | Frame medio | Scripts | Render |
|---|---:|---:|---:|---:|---:|---:|
| Primera | 199 | 133–171 | 42–53 | 26,58 ms | 14,00 ms | 7,61 ms |
| Tras corregir la actualización global de transforms | 200 | 159–192 | 46–58 | 25,13 ms | 10,16 ms | 8,88 ms |
| Tras omitir el scheduler vacío y reutilizar índices de luces | 300 | 252–297 | 76–92 | 23,66 ms | 10,49 ms | 6,64 ms |

La primera exportación también contiene 101 frames sin simulación, con media de 15,98 ms. Mezclarlos con Play produce la media de 23,02 ms mostrada en la primera captura. La segunda captura muestra estadísticas de los últimos 120 frames, con media de 24,97 ms; su JSON contiene 200 frames. Las cargas son diferentes, por lo que la diferencia entre medias no demuestra por sí sola una mejora causal.

## Trabajo redundante confirmado y corregido

1. **Sincronización de posición:** la corrección F06 llamaba `TransformSystem::update` para toda la escena por cada movimiento de un body desde scripting. Ahora actualiza sus ancestros y descendientes mediante `update_subtree`, conservando coordenadas locales/world, rotación, escala y visibilidad. En una prueba aislada Debug, 30 movimientos por frame con 150 entidades bajaron de 7,337 a 0,180 ms; con 1.000 entidades, de 48,454 a 0,202 ms. Ambos ejecutables produjeron el mismo checksum. La regresión de jerarquía verifica además los valores completos relevantes, no sólo ese checksum.
2. **Scheduler vacío:** `ScriptRuntime::update` sincronizaba propiedades y componentes de todos los behaviours antes y después del scheduler aun cuando no contenía acciones. Ahora omite esa fase cuando la cola está vacía. Las acciones canceladas cuentan como trabajo en cola hasta que `step` las libera; una acción publicada después de la consulta se procesa en el siguiente tick con la sincronización correspondiente.
3. **Topología de luces:** cada luz radial reconstruía los mismos 18.144 índices de triángulos por frame. Ahora se construyen una vez y se comparten como datos CPU inmutables. Los vértices, parámetros de luces, índices y orden de dibujo conservan su resultado; no se reduce la resolución ni la geometría.

4. **Decodificación de partículas:** las vistas de componentes inyectadas en constructores vuelven a cargar los valores por defecto en cada callback. Ahora cada vista conserva el contenido fuente y los dos valores decodificados. Se valida el registro y se lee el archivo para detectar cambios por contenido, incluso si tamaño y fecha coinciden; sólo se repite el parseo/validación cuando cambian los bytes. Archivos ausentes, inválidos o handles incompatibles producen cero como antes. Los overrides se aplican después de resolver los valores por defecto.

El segundo muestreo mostró costos altos en la sincronización duplicada de campos y la lectura de efectos de partículas desde `begin_invocation`, y en los `vector::insert` que reconstruían la topología radial. El tercero volvió a mostrar decodificación de partículas durante gameplay, aunque también incluye espera de presentación y salida de Play: no representa una sesión sostenida homogénea. La lectura del archivo por callback sigue pendiente de optimización mediante un contrato de invalidación compartido.

## Energía y condiciones de comparación

El usuario advirtió que había alternado batería y alimentación. La consulta local posterior confirmó `lowpowermode 1` para batería y `lowpowermode 0` para AC. La máquina estaba conectada durante esa consulta; no se registró la alimentación de cada captura anterior. Esta diferencia puede contribuir a la variación, pero no permite atribuir retrospectivamente un porcentaje al modo de energía. No se cambiaron preferencias del sistema.

Para evaluar equipos más modestos:

1. Usar la misma versión, escena, resolución física, cámara y recorrido; registrar entidades, scripts, cuerpos, partículas, draw calls, backend y estado de energía. Evitar builds y tests simultáneos a la captura.
2. Medir por separado editor Debug y juego exportado Release. El presupuesto objetivo para 60 FPS es 16,67 ms por frame; registrar media, P95/P99, frames que exceden el presupuesto y memoria durante sesiones largas. Los scopes son inclusivos y anidados: no sumar `simulation.tick`, `scripts` y `runtime.update` como costos independientes.
3. Repetir batería/AC con el mismo recorrido y luego medir en el equipo mínimo real. El modo de bajo consumo de esta Mac sirve como escenario adicional; no representa automáticamente una máquina más débil.
4. Priorizar sincronización de scripts y lecturas de assets por callback; después medir batching, luces, partículas y resolución. Cualquier perfil de calidad debe reducir costo visual de manera explícita y conservar la lógica de simulación.

## Validación y límites

- La corrección de transforms pasó cuatro pruebas específicas y la suite Debug completa de 95 pruebas antes del segundo ajuste.
- Se amplió la prueba de Play Mode con la transición de scheduler activo a vacío y vuelta a una tarea asíncrona, verificando que continúan las actualizaciones y la sincronización de propiedades.
- Para el segundo ajuste se detuvo deliberadamente el build aislado duplicado y se usa el build habitual de `dev.sh`. En éste hay 94 pruebas CTest porque el test GPU es optativo; el ejecutable GPU se verifica aparte. Resultado final: **94/94 CTest y GPU Metal aprobados**.
- El ajuste de partículas pasó el build habitual, **94/94 CTest (71,37 s)** y el smoke GPU Metal. La regresión ejecuta EngineScript nativo con cambios de contenido manteniendo tamaño/fecha, overrides, JSON malformado, esquema inválido, borrado/restauración y handles inválidos o de otro tipo. No se midió todavía su ahorro de tiempo en una sesión controlada del juego.
- La tercera captura todavía promedia 23,66 ms con más carga. La recuperación de 60 FPS requiere una nueva captura del juego con el ajuste de partículas y sin compilaciones ni pruebas compitiendo por CPU. No se considera demostrada por la compilación ni por la mejora del microbenchmark.
- Los picos de eventos de aproximadamente 24 ms de la primera captura son distintos del costo sostenido y requieren seguimiento si persisten.

Las capturas y logs locales se retiraron del repositorio al cerrar este trabajo.
Las tablas conservan sus resultados y limitaciones; una nueva comparación debe
usar capturas obtenidas bajo las condiciones descritas arriba.
