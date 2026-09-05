# Correcciones de la auditoría de Engine2D

La referencia original es `60a8034b908b9d19e79d889b6ff80b3a7fb5e063`. Este documento sigue la resolución de los 27 hallazgos de [AUDITORIA.md](AUDITORIA.md). Los cambios pertenecen al motor y sus pruebas; la validación usa proyectos temporales, sin modificar el juego externo.

Estado de validación: **27 hallazgos corregidos, con las verificaciones indicadas abajo**.

El seguimiento de la regresión de FPS, sus capturas y las correcciones posteriores se documentan en [RENDIMIENTO.md](RENDIMIENTO.md).

## Seguimiento: shaders recreados al instanciar materiales (2026-09-05)

El log del juego mostró creaciones GPU repetidas de Energy Trail, Radial Glow y Spider Telegraph. `MaterialAsset::load` generaba un objeto FragmentShader nuevo por carga, mientras que el backend identificaba shaders por su puntero. Además, el backend invalidaba el estado de shaders de cero samplers al alternar materiales.

`ShaderAsset::load` ahora reutiliza objetos inmutables con nombre, layout, variantes, rutas, entry points y bytes iguales. La tabla sólo mantiene referencias débiles. Se siguen leyendo y validando las fuentes: cambiar bytes/layout genera otra versión y un archivo ausente produce error. La reutilización se limita a cero/una textura para conservar el estado específico de los materiales con samplers adicionales. El backend acepta también cero samplers al reutilizar su estado entre materiales.

Regresiones: 100 cargas de material comparten el shader; cambios de bytes/layout y eliminación de fuente se detectan. En Metal, 300 binds alternados entre materiales de cero/una textura crean exactamente dos shaders. La suite Debug completa volvió a pasar **95/95** con este cambio; las dos regresiones específicas también pasaron en Release con UBSan (**2/2**). Las capturas posteriores de la partida se analizan en [RENDIMIENTO.md](RENDIMIENTO.md).

Los resultados de la tabla original corresponden al cierre de los 27 hallazgos; las validaciones posteriores se indican por separado en este seguimiento y en el informe de rendimiento.

## Resolución por hallazgo

| ID | Corrección aplicada | Regresión o evidencia |
|---|---|---|
| F01 | Las escrituras comprueban `flush` y `close`. El reemplazo atómico usa un temporal exclusivo en el mismo filesystem y sólo sustituye el destino tras cerrar correctamente. | `CoreIntegrityTests`: límite de archivo en proceso hijo, original conservado, cuatro escritores simultáneos, rutas con y sin directorio. |
| F02 | Escenas, saves binarios y ProjectSettings usan la escritura atómica corregida. | Regresión de filesystem y suites de escenas, prefabs, autosave y settings. |
| F03 | Cada voz SFX posee sonido, decoder y bytes mediante RAII; se recolecta al terminar y se libera antes de apagar miniaudio. Application ejecuta la recolección por frame. | `CoreIntegrityTests`: 20 ciclos de WAV desde memoria y archivo en mixer offline, datos inválidos, apagado durante reproducción y reinicio. |
| F04 | Dispatch recorre una instantánea de suscriptores con estado de cancelación compartido. Los eventos nuevos se difieren; `clear` cancela también el resto del lote activo. | Regresiones de subscribe/unsubscribe/queue/clear dentro de callbacks. |
| F05 | `parallel_for` espera todos los futures ya enviados antes de propagar una excepción, incluso si falla el tramo del llamador o un enqueue. Conserva el mismo callable. | Excepciones en el llamador y un worker, comprobando finalización de los otros tramos antes del retorno. |
| F06 | La sincronización de física convierte poses world a local y evalúa padres antes que hijos. El setter de posición local sincroniza el body desde el world calculado. | Padre dinámico, hijo dinámico, movimiento del padre y setter del hijo; suites de jerarquía y Play Mode. |
| F07 | Cada body registra la generación de la entidad. Un ID reciclado obliga a reconstruir cuerpo y joints; limpiar/reconstruir usa también los endpoints físicos de joints. | Destruir y recrear el mismo ID en otra posición; suites de física/componentes. |
| F08 | Las instancias se separan de la lista antes de `on_destroy`; se cancelan sus tareas y se mantienen bindings y módulo vivos hasta terminar. La destrucción de subárbol limpia descendientes antes de eliminar el ECS. | Tres generaciones de behaviours realmente compilados, solicitudes de destroy solapadas, cero instancias restantes y callbacks una vez por instancia. |
| F09 | Un registro existente corrupto/incompatible hace fallar el scan sin reemplazar identidades ni archivo. La carga se hace en un candidato. | JSON corrupto y versión de tipo incorrecto conservan handles y contenido. |
| F10 | El compilador y linker se ejecutan con argv mediante `posix_spawnp`/`CreateProcessW`. El nombre de módulo se escapa como literal C++. | Compilación y carga real en ruta con espacios, `$()`, backticks y comillas; comprobación de compilación del camino Windows con MinGW. |
| F11 | Validación común para registro, SceneValidator y física: datos finitos, dimensiones positivas y polígonos de 3–8 puntos distintos no colineales. Se comprueba geometría escalada antes de Box2D. | Polígono de nueve puntos, colineal y NaN rechazados; triángulo válido aceptado sin body inválido. |
| F12 | El merge de colliders usa un conjunto de celdas ocupadas y orden determinista; no reserva el rectángulo envolvente. Las expansiones usan enteros de 64 bits. | Tiles separados 65.535 celdas y coordenadas en los extremos de `int`, con tres colliders en lugar de una asignación densa. |
| F13 | `is_alive` usa un índice hash mantenido en create/destroy/clear/swap, conservando el orden de iteración de entidades. | Regresiones de orden, reciclado y clear; suites ECS/stress. Complejidad esperada O(1) por consulta. |
| F14 | Todos los tamaños de batch comparten preparación, máscaras y `SpriteSortKey`, conservando campos completos del comando. La generación de vértices sigue paralela. | `RenderMetricsTests`: 511/512/1024 sprites, capas ocultas y lectura de píxeles que verifica el orden explícito; suites de materiales/GPU. |
| F15 | La textura blanca pertenece a cada Renderer2D; se libera al invalidar recursos o destruirlo. Se elimina el singleton `Texture::get_white`. | Dos renderers simultáneos dibujan materiales procedurales; lectura de píxeles y ciclo invalidar/restaurar. |
| F16 | Reconstruir dependencias vuelve a evaluar registros MissingDependency, permitiendo recuperación transitiva. Se siguen leyendo referencias de formatos desconocidos como `.data`. | Eliminar/reimportar dependencia recupera al dependiente; `AssetDatabaseTests` y `AssetFileCommandTests`. |
| F17 | Las mutaciones se preparan y persisten en un candidato antes de aplicar registros/índices. Reimportaciones conservan las direcciones de registros existentes cuando no hay crecimiento. | Destino de DB bloqueado: move/remove/import/settings fallan sin alterar el estado en memoria; suites de comandos/materiales. |
| F18 | Preferencias, descriptor, settings y DB validan tipos y contienen excepciones JSON. Settings/prefs conservan el último estado válido ante fallo. | Arrays donde se requieren objetos, campos con tipos erróneos, vectores inválidos y carga parcial fallida. |
| F19 | SaveSystem rechaza versiones distintas de la soportada antes de procesar el payload. | Save con versión 999 y CRC intacto rechazado. |
| F20 | El contador de IDs se mantiene en 64 bits y detecta agotamiento antes de convertir a EntityID; nunca asigna cero por overflow. | Restaurar ID próximo al máximo, alcanzar máximo y rechazar el siguiente. |
| F21 | El mantenimiento de bodies exige Transform, RigidBody y Collider, además de entidad/generación válidas. | Quitar Collider directamente del ECS elimina el body al siguiente update. |
| F22 | Setter y lectura de velocidad/impulso angular usan la misma convención de signo. | Solicitar +90 grados/s devuelve +90 tras avanzar física. |
| F23 | El checkout fijado de shadercross deja de usar un clon shallow que excluía el commit. | Descarga y build nuevo del toolchain y sus submódulos. |
| F24 | Los verificadores macOS/Windows exigen recursos de shader propio sólo cuando el descriptor declara `runtime_shader`. | Exportación macOS sin shader propio; verificación adicional del caso con shader. |
| F25 | Todos los targets de tests mantienen asserts activos mediante `-UNDEBUG` o `/UNDEBUG`, también en Release. | `CoreIntegrityTests` falla al compilar si NDEBUG sigue definido; suite Release con UBSan. |
| F26 | PackageMacOSApp incluye `Engine/`; el verificador exige `Engine/Shaders/lit_sprite.frag.msl`. | Inspección del bundle, verificador y smoke. |
| F27 | `e2sc --native-module` y NativeModuleBuilder generan el entrypoint de un módulo vacío si no hay scripts. Hot reload rechaza perder fuentes aún referenciadas por slots activos. | Compilar/cargar módulo vacío, export/smoke sin scripts y `ScriptHotReloadTests`. |

## Validación reproducible

Las regresiones nuevas están en `tests/CoreIntegrityTests.cpp`, `tests/RenderMetricsTests.cpp`, `tests/EngineScriptPlayModeTests.cpp` y `tests/GPURenderSmokeTests.cpp`. Sustituyen los probes auxiliares de la auditoría original como puerta de aceptación del código corregido.

El fixture `tests/fixtures/runtime_project` contiene bytes de shader de prueba. Para exportar realmente se usa una copia temporal sin `runtime_shader`, o se sustituye el shader por HLSL real en otra copia. No se modifica el fixture de mocks ni se exporta el juego externo.

```sh
cmake -S . -B <build-audit> -DCMAKE_BUILD_TYPE=Debug \
  -DENGINE_PROJECT_DIR=<fixture-temporal> -DENGINE_ENABLE_HARDWARE_TESTS=ON
cmake --build <build-audit> --parallel 4
ctest --test-dir <build-audit> --output-on-failure
cmake --build <build-audit> --target smoke_macos_export
```

Para Release se configura un directorio independiente con `-DCMAKE_BUILD_TYPE=Release` y `-DCMAKE_CXX_FLAGS="-fsanitize=undefined -fno-sanitize-recover=all"`. No ejecutar suites simultáneas sobre los mismos directorios temporales de fixtures.

| Verificación | Resultado |
|---|---|
| Build completo Debug del motor, editor y herramientas | Pasó. |
| Ejecutable habitual `build/Engine2D` | Actualizado por la sesión activa de `dev.sh`; comprobación de dependencias con `make -q` aprobada para EngineRuntime y Engine2D, binario Mach-O arm64. |
| CTest Debug con GPU Metal | **95/95**. |
| Build Release con UBSan y asserts activos en tests | Pasó. |
| CTest Release + UBSan con GPU Metal | **95/95**, sin diagnósticos UBSan; se recompiló y repitió MaterialPackTests tras el último ajuste de ThreadPool (**1/1**). |
| Comprobación de flags de compilación Release | **93 unidades de tests** con `-UNDEBUG` efectivo. |
| Shadercross sin caché previa | Build completo correcto del commit fijado y submódulos; variantes SPIRV, DXIL y MSL compiladas. |
| Paquete macOS sin scripts ni shader propio | Verificador, firma ad-hoc y smoke desde copia aislada pasaron. |
| Paquete macOS con shader HLSL real y behaviour nativo | Verificador y smoke pasaron; el bundle ejecutó `on_init` y emitió `EXPORT_PROBE_STARTED`. |
| Paquetes con shader interno/declarado eliminado | Rechazados por el verificador, como se esperaba. |
| Camino Windows del lanzamiento de procesos | Compilación sintáctica C++20 con MinGW correcta; no se ejecutó en Windows. |
| `git diff HEAD --check` | Pasó. |

El primer intento de exportación detectó una escena ausente en el fixture temporal; se restauró desde la copia de pruebas antes de repetir. No se debilitó el verificador. Los tiempos de las suites no se usan como benchmark del motor.

La actualización del build habitual coincidió con una compilación iniciada por `dev.sh` y el primer enlace del archivo estático falló. Se dejó terminar esa compilación y se verificó el resultado sin lanzar otra instancia del editor; las suites de aceptación se ejecutaron en directorios aislados.

Los resultados se resumen en esta tabla. Los logs y fingerprints propios de la sesión se retiraron del repositorio; los tests y el workflow conservan la validación reproducible.

## Contratos y límites

- Escritura atómica significa que un fallo detectado antes del reemplazo conserva el archivo anterior. No se promete durabilidad ante corte eléctrico: no se añadió un protocolo de `fsync` de archivo/directorio ni se simuló un corte real.
- AudioEngine mantiene su interfaz de uso desde el hilo principal. El modo offline mezcla estéreo float a 48 kHz y permite comprobar ownership sin abrir un dispositivo. Estas pruebas no miden latencia ni calidad audible.
- La preparación de comandos de sprites es secuencial para conservar orden y estado; la generación de geometría sigue paralela. El resultado se verifica por píxeles; no se infiere una mejora de FPS de estas regresiones.
- Los colliders dispersos consumen memoria en función de tiles ocupados. Las coordenadas extremas prueban seguridad del algoritmo, no precisión física ilimitada en float.
- ASan quedó bloqueado en la inicialización de su runtime en esta máquina durante la auditoría original. No se afirma una pasada ASan/LSan/TSan ni ausencia universal de fugas o carreras.
- El workflow `.github/workflows/engine-integrity.yml` agrega build, UBSan, CTest y export macOS en Debug/Release; no se ha ejecutado en GitHub desde esta sesión. Se desactivan allí los tests que requieren GPU nativa.
- Compilar el camino Windows no equivale a probar un runtime Windows. Siguen pendientes máquinas limpias, sesiones largas, dispositivos móviles y firma de distribución. La resolución de estos bugs no certifica por sí sola preparación productiva para cualquier juego.
