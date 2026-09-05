# Auditoría integral de Engine2D — 4 de septiembre de 2026

Informe histórico del checkout original. Los logs y probes auxiliares se retiraron del repositorio al consolidar las regresiones en `tests/`. Seguimiento de correcciones posteriores: [CORRECCIONES.md](CORRECCIONES.md). Los hallazgos y cifras siguientes corresponden al estado auditado original.

## Dictamen

**El motor es viable para desarrollar juegos 2D propios, pero este estado no debe considerarse una base estabilizada para producción.** La arquitectura y las pruebas permiten avanzar; los fallos de persistencia, ciclo de vida, física y distribución requieren una fase explícita de corrección.

Se registran **27 hallazgos: 14 P1 y 13 P2**. En 24 hay reproducción ejecutada, fallo de build o comprobación de artefacto; tres se sustentan en inspección del código. No se encontraron pruebas de un P0, entendido como fallo universal que impida usar cualquier proyecto. No se declara ausencia de otros defectos.

La suite limpia pasó **94/94 pruebas en 97,09 segundos**, incluida una prueba de GPU real en **Metal**. Las pruebas nuevas de esta auditoría detectaron fallos fuera de esa cobertura. El resultado verde de CTest no invalida esos hallazgos.

## Alcance y método

- Commit auditado: `60a8034b908b9d19e79d889b6ff80b3a7fb5e063`.
- Entorno: macOS ARM64, AppleClang 17, C++20, configuración Debug para el build general.
- Inventario: 483 archivos C++/headers bajo `src/`; revisión transversal por subsistemas, contratos y rutas de alto riesgo. Este número es inventario, no una afirmación de revisión manual de cada línea.
- Lectura de código, seguimiento de llamadas y ownership, compilación desde un directorio vacío, suite completa, GPU, empaquetado, entradas inválidas, inyección acotada de errores de escritura, pruebas de límites y reproducciones de crashes en procesos hijos.
- Los probes de ECS/rendimiento se compilaron con `-O2`; los de física/runtime adicionales con `-O1`. No se extrapolan sus tiempos a FPS de un juego.
- Sólo se añadieron este informe, fuentes de reproducción y evidencias. No se corrigió código del motor ni se modificó el proyecto externo `projecto`.
- El proyecto de prueba fue una copia temporal de `tests/fixtures/runtime_project`. Se retiró de esa copia `runtime_shader`, porque su HLSL contiene literalmente `fixture` y sirve para pruebas con compilador simulado, no para shadercross real. Se compilaron normalmente los shaders internos y de pruebas.
- El target general copia `compile_commands.json` a la raíz del checkout. Tras la auditoría se restauró desde el build habitual, evitando dejar el IDE apuntando al build temporal.

**P1:** corregir antes de comprometer una entrega que dependa de esa ruta; riesgo de pérdida de datos, crash, ejecución inesperada o comportamiento central incorrecto. **P2:** defecto real o brecha de validación que debe corregirse antes de depender de la función afectada. La prioridad no implica que todos los proyectos activen todos los fallos.

## Resultados de validación

| Validación | Resultado y límite |
|---|---|
| Build limpio con toolchain descargado por defecto | Falló al hacer checkout de SDL_shadercross; F23. |
| Build limpio usando shadercross local fijado al mismo commit | Pasó, incluyendo editor, runtime y tests; fuentes del motor compiladas de nuevo. |
| CTest Debug con hardware habilitado | 94/94, 97,09 s; sin pruebas omitidas. |
| GPU Metal | Pasó: sprites, globals procedurales, material pack, sombras realtime/baked, Canvas, fullscreen, blending y zoom. No equivale a validar todos los efectos o drivers. |
| Probes adicionales | Fallos de escritura, lifecycle, física, assets, batch, tipos JSON y crashes reproducidos. Ver evidencias. |
| UBSan focalizado | Confirmó overflow de entero en el tilemap, F12. |
| ASan | Bloqueado antes de `main` durante inicialización de shadow memory del runtime local. Los procesos agotaron el timeout; no cuentan como fallos del motor ni como validación de memoria. Se guardó un muestreo de pila. |
| Empaquetado macOS de fixture sin shader propio | Se creó bundle ad-hoc; verificador falló por exigir `runtime_shader`, F24. Además faltan shaders internos en el bundle, F26. |
| Smoke directo del bundle sin scripts | Falló por exigir una biblioteca de gameplay que el compilador no genera para proyectos sin scripts, F27. |
| Windows/Linux/iOS/Android | Inspección de soporte y packaging disponible; no hubo ejecución en esas plataformas. |

Las observaciones relevantes de los logs originales se resumen en cada hallazgo; la validación del código corregido está en [CORRECCIONES.md](CORRECCIONES.md).

## Cobertura por subsistema

| Subsistema | Trabajo realizado | Límites / conclusión |
|---|---|---|
| Filesystem, saves, preferencias | Revisión de escrituras/lecturas, límites de archivo, corrupción y tipos | F01, F02, F18, F19. Falta probar recuperación ante corte eléctrico real. |
| ECS, jerarquía y transforms | IDs/generaciones, iteración, acceso, componentes, pruebas y benchmark | F13, F20; interacción física F06/F07/F21. |
| Escenas, validación y prefabs | Migraciones, staging/rollback de carga, lifecycle y suites | Buen soporte transaccional de carga; guardado F02 y validadores incompletos F11/F12. |
| Física | Creación/reciclado de bodies, sincronización local/world, colliders, velocidad angular | F06, F07, F11, F21, F22; no hubo prueba prolongada de contactos/joints en un juego. |
| Eventos y concurrencia | Mutación durante dispatch, futures, excepciones, jobs y scheduler | F04/F05 reproducidos; no hubo campaña completa TSan ni prueba de todas las intercalaciones. |
| EngineScript frontend | Parser, resolver, tipos, IR, APIs, cache y tests/fuzz existentes | Suite completa verde; no es fuzzing abierto durante horas ni revisión formal del compilador. |
| EngineScript nativo/runtime | C++ generado real, carga de módulo, destructores, hot reload, async/DI | F08/F10; reproducción con dos behaviours compilados, no sólo mocks. |
| Assets | Identidad, corrupción, dependencias, import/move y fallo de persistencia | F09, F16, F17. Escaneo sigue leyendo todos los assets al reconstruir dependencias. |
| Renderer/materiales/lighting | Batch y orden, ownership, pools, shaders, suites y GPU Metal | F14/F15/F26; no se validaron todos los backends ni cada combinación visual. |
| Partículas, trails, VFX y superficies | Lectura de límites/ownership y suites actuales | Pruebas pasan; la fuga de audio es independiente. No se validó visualmente cada preset. |
| Tilemaps | Revisión de generación de colliders y datos dispersos | F12; límites de extensión y coste requieren contrato explícito. |
| Audio | Recorrido completo de init/play/shutdown y ownership miniaudio | F03 por inspección. No se abrió dispositivo ni se evaluó calidad/latencia de audio. |
| UI/input/display | Lectura de foco, mouse, Canvas, mapeo de viewport y suites | Tests y GPU Canvas pasan; no se certifican interacción humana, gamepads ni DPI de todas las pantallas. |
| Editor/workspace/undo/AI | Historial transaccional, archivos, recuperación, cancelación y suites | F17/F18; no se contactaron proveedores externos ni se aprobó autoría AI. |
| Build/export/plataformas | Build vacío, toolchain, paquete y smoke macOS; lectura Windows | F23–F27; faltan máquinas limpias, firma de distribución y validación multiplataforma real. |

## Hallazgos P1

### F01 — La escritura confirma éxito antes de detectar errores de cierre

**Evidencia:** reproducción `core_probe write`, `resultados` (reproducción original). Tanto `write_text` como `write_text_atomically` devolvieron `true` al intentar 1.000 bytes cuando el proceso sólo podía escribir 64. Se usó `RLIMIT_FSIZE` únicamente dentro del proceso hijo y un archivo temporal.

**Ubicación:** `src/core/FileSystem.cpp:151`, `:176`. El estado se consulta antes de vaciar/cerrar el stream. El destructor puede fallar al escribir el búfer sin que se compruebe el resultado. La variante atómica luego renombra el temporal incompleto sobre el archivo bueno. `write_bytes` presenta el mismo patrón por inspección.

**Impacto:** guardados incompletos informados como exitosos; alcanza preferencias, AssetDatabase, autosave y comandos que confían en esas funciones. `EditorLayer::save_scene` marca limpio el historial y elimina recuperación tras el éxito reportado.

**Corrección:** comprobar explícitamente flush y cierre antes del reemplazo; conservar el original si cualquiera falla, limpiar temporales y propagar el error. Usar nombres temporales únicos si hay escritores concurrentes. Separar atomicidad de durabilidad y definir si se exige sincronización a disco.

**Regresión requerida:** archivo original intacto y retorno `false` bajo escritura parcial/error de cierre; cobertura equivalente para texto, bytes y consumidores críticos.

### F02 — El guardado normal de escenas trunca el original

**Evidencia:** inspección de `src/scene/SceneSerializer.cpp:109` y `src/core/FileSystem.cpp:146`, llamada desde `src/editor/EditorLayer.cpp:1308`.

`SceneSerializer::serialize` usa `write_text`, que abre con `std::ios::trunc`. Un cierre del proceso o error de escritura durante Save deja el único archivo original vacío o parcial. Las protecciones contra conflicto externo y contra Save durante Play no cubren esta ventana.

**Corrección:** usar escritura atómica ya corregida por F01 y un contrato de recuperación. **Regresión:** interrumpir/fallar antes del reemplazo sin alterar la escena previa; no limpiar dirty/autosave ante fallo. No se interrumpió ningún editor real durante esta auditoría.

### F03 — Los efectos de sonido no liberan sus recursos

**Evidencia:** inspección completa de `src/audio/AudioEngine.cpp:73` y `:89`. En el camino exitoso `play_sound` pierde el puntero obtenido con `new ma_sound`. `play_sound_from_memory` pierde además decoder y copia del buffer. No se registran voces, callbacks de recolección ni ownership que permita liberarlos. `shutdown` sólo libera explícitamente `s_music` antes del engine.

**Impacto:** fuga acumulativa por cada SFX, particularmente grave con pasos, disparos o impactos. La limpieza interna de sonidos inline de miniaudio no corresponde a estos `ma_sound` creados manualmente. El camino de fallo posterior a inicializar decoder tampoco llama a `ma_decoder_uninit` antes de borrarlo.

**Corrección:** voces con ownership explícito, recolección al terminar, uninit en orden correcto y limpieza total al apagar. **Regresión:** reproducir miles de sonidos y verificar que voces/allocations regresan a la línea base, sin trabajo pesado de liberación dentro del callback de audio. No se midió el tamaño real de esta fuga en hardware.

### F04 — Suscripciones y eventos reentrantes invalidan iteradores

**Evidencia:** `core_probe event-subscribe` terminó con `std::bad_function_call`/SIGABRT; `event-queue` terminó con SIGSEGV. `Resultados` (reproducción original).

**Ubicación:** `src/events/EventDispatcher.hpp:50` y `:68`. Se recorren directamente vectores de suscriptores/eventos mientras los callbacks pueden modificarlos mediante `subscribe`, `unsubscribe` o `queue`. Una realocación invalida el recorrido; quitar una suscripción puede destruir el callback que está ejecutándose.

**Corrección:** snapshot o almacenamiento estable de suscripciones con semántica documentada; intercambiar la cola pendiente por una cola local antes de ejecutar. Definir si nuevas suscripciones/eventos aplican a la próxima ronda. **Regresión:** alta/baja propia y ajena, publicación anidada y enqueue dentro de callbacks, sin crash, pérdida ni duplicación de entregas.

### F05 — `parallel_for` deja tareas usando referencias tras lanzar una excepción

**Evidencia:** `core_probe thread-exception` terminó con SIGSEGV. Un worker permanece activo mientras el chunk del llamador lanza; al volver por excepción, el llamador destruye su función y el worker la vuelve a usar.

**Ubicación:** `src/core/ThreadPool.cpp:101`, `:113`, `:118`. La captura `&job` no mantiene viva la función; una excepción antes de completar todos los `future.get()` abandona el join lógico. Los futures de packaged tasks no aseguran esperar al destruirse.

**Corrección:** esperar todas las tareas en todas las salidas, conservar la primera excepción y relanzarla después; asegurar ownership del callback y del estado capturado. **Regresión:** excepción en chunk principal y en cada worker, tareas lentas y comprobación de que no existe trabajo pendiente al retornar.

### F06 — Física escribe coordenadas world dentro del transform local

**Evidencia:** `physics-parent` (reproducción original): padre en X=100, hijo local X=10, gravedad y delta cero; antes world=110, después world=210 y local=110.

**Ubicación:** `src/physics/PhysicsSystem.hpp:700`. Se crea el body con `world_position`, pero se copia la posición de Box2D directamente a `transform.position`. Lo mismo requiere revisión para la rotación. La siguiente propagación de jerarquía aplica otra vez el transform del padre.

**Corrección:** convertir el estado físico world a local respecto del padre, con el mismo contrato de transforms usado por el editor, o rechazar jerarquías físicas no soportadas. **Regresión:** padres trasladados, rotados y escalados; bodies dynamic/kinematic sin movimiento no deben saltar.

### F07 — Un ID reciclado reutiliza el cuerpo físico de otra entidad

**Evidencia:** `physics-reuse` (reproducción original): crear body en X=10, destruir entidad y crear otra en X=500 antes del siguiente tick; se reutiliza ID y la nueva termina en X=10.

**Ubicación:** `src/physics/PhysicsSystem.hpp:535`, `:553`. La limpieza sólo consulta `is_alive(id)` y el mapa de bodies sólo usa ID; no contrasta `entity_generation`. Si el ID vuelve a estar vivo antes de limpiar, conserva cuerpo, fixtures y estado anterior.

**Corrección:** asociar generación a cada body y destruir/recrear ante cambio de vida, o integrar destrucción física explícita con el lifecycle ECS. Revisar joints y otras cachés con claves sólo por ID. **Regresión:** destroy/spawn en el mismo frame con posición, forma, velocidad y tipo distintos.

### F08 — Destruir entidades omite `on_destroy` y deja scripts de descendientes vivos

**Evidencia:** `script_probe` (reproducción original), con dos behaviours EngineScript compilados a C++ y cargados de verdad. Al destruir el padre: cero llamadas a `on_destroy` frente a dos esperadas, cero entidades y una instancia de script todavía registrada. El hijo recibe su callback recién en shutdown.

**Ubicación:** `src/scripting/ScriptRuntime.cpp:2659`, `:2704`. `discard_entity_instances` cancela tareas y borra instancias sin `on_destroy`; `flush_pending_entity_destroys` lo invoca sólo para la raíz antes de destruir todo el subtree ECS.

**Impacto:** cleanup de gameplay omitido, registros estáticos que no se limpian y referencias/coroutines asociadas a descendientes sin entidad. El camino de quitar un behaviour sí llama a `on_destroy`, por lo que hay contratos divergentes.

**Corrección:** una sola operación de destrucción que enumere el subtree, ejecute cleanup exactamente una vez, cancele tareas y elimine registros antes de borrar datos. Contener errores del callback sin abandonar la limpieza. **Regresión:** padre/hijos con behaviours, referencias estáticas y tareas pendientes; cero instancias huérfanas después del tick.

### F09 — Un AssetDatabase corrupto se trata como nuevo y se sobrescribe

**Evidencia:** `core_probe asset-corrupt`: `handle changed=1 corrupt database overwritten=1`.

**Ubicación:** `src/core/AssetDatabase.cpp:83`, `:111`, `:243`. `load_database` limpia el estado y devuelve `false` tanto cuando falta el archivo como cuando existe pero es inválido/incompatible. `scan` continúa generando handles nuevos y guarda encima.

**Impacto:** se pierde el mapa estable del que dependen escenas/prefabs; una corrupción del registro puede convertir referencias antes válidas en referencias rotas. No hay que confundir este archivo con una caché totalmente reconstruible.

**Corrección:** distinguir inexistente, incompatible y corrupto; no sobrescribir los dos últimos; conservar copia recuperable y fallar con diagnóstico. **Regresión:** JSON truncado, versión incompatible y registros inválidos deben conservar bytes originales e identidades anteriores.

### F10 — Las rutas de compilación EngineScript se interpretan como shell

**Evidencia:** `shell-path` (reproducción original). Se usó un directorio cuyo nombre literal contiene `$(printf EXPANDED)`. El compilador recibió una ruta con `EXPANDED` en su lugar: el shell ejecutó el `printf`. Fue una prueba inocua en un directorio temporal, sin red ni acceso a datos sensibles.

**Ubicación:** `src/enginescript/module/NativeModuleBuilder.cpp:25`, `:429`, `:464`. Rodear una ruta con comillas dobles no neutraliza sustituciones del shell; luego se llama a `std::system`.

**Impacto:** proyectos con ciertos caracteres fallan al compilar; rutas controladas por un tercero pueden ejecutar comandos con permisos del usuario al dispararse el build. EngineScript nativo ya supone confianza en el código, pero un nombre de directorio no debería convertirse accidentalmente en comandos.

**Corrección:** lanzar compiler/linker mediante argv y API de procesos, sin shell. **Regresión:** espacios, comillas, dólar, backticks y caracteres Unicode permanecen argumentos literales tanto en compile como en link.

### F11 — Los polígonos aceptados exceden el límite de Box2D

**Evidencia:** `bounds-polygon` (reproducción original): un polígono de nueve vértices termina en assert de `b2PolygonShape::Set`, cuyo límite es ocho.

**Ubicación:** `src/scene/BuiltinComponentRegistrations.cpp:1126` sólo exige al menos tres vértices; `src/physics/PhysicsSystem.hpp:838` pasa todos a Box2D. También faltan controles adecuados para degeneración/finitud en esa validación.

**Corrección:** validar el contrato completo antes de modificar la escena o crear fixtures; si se soportan polígonos complejos, descomponerlos explícitamente. **Regresión:** 2, 3, 8, 9 vértices, duplicados, colineales y valores no finitos. No confiar en que Release elimine el assert: el vendor limita el número procesado y puede cambiar la forma.

### F12 — Dos tiles alejados pueden desbordar la memoria del generador de colliders

**Evidencia:** `bounds-tilemap` (reproducción original), UBSan: `65536 * 65536` desborda `int`. Basta colocar tiles en (0,0) y (65535,65535).

**Ubicación:** `src/tilemap/TilemapColliderSystem.cpp:35`. El mapa es disperso, pero el builder reserva una grilla densa de `w*h` con enteros de 32 bits y recorre todo el rectángulo. Aun sin overflow, un par de tiles puede disparar memoria/CPU desproporcionadas.

**Corrección:** límites explícitos y multiplicación comprobada antes de reservar; generación por chunks o regiones ocupadas. **Regresión:** coordenadas extremas con pocos tiles deben rechazarse de forma acotada o procesarse sin coste proporcional al área vacía.

### F23 — El build vacío no puede obtener la revisión fijada de shadercross

**Evidencia:** `build inicial` (reproducción original): `fatal: reference is not a tree` y `Failed to checkout tag: e55cf5e...`. El mismo commit sí existe en el checkout local del toolchain ya usado por el motor.

**Ubicación:** `cmake/ShaderCompilation.cmake:41`: hash fijado con `GIT_SHALLOW TRUE`. La descarga superficial actual no incorpora ese commit histórico. Usar explícitamente el ejecutable local permitió continuar sin cambiar fuentes.

**Corrección:** obtener realmente la revisión fijada —clon no superficial, ref adecuada o artefacto versionado con checksum— y probar bootstrap sin caché. **Regresión:** configurar y compilar en directorio nuevo/máquina limpia con los únicos prerrequisitos documentados.

### F26 — El bundle macOS omite los shaders internos que sí existen en staging

**Evidencia:** `packaged-shaders` (reproducción original): staging contiene `Engine/Shaders/lit_sprite.frag.msl`; no aparece en `Contents/Resources/Engine/Shaders` ni `Contents/MacOS/Engine/Shaders` del bundle generado.

**Ubicación:** `cmake/PackageMacOSApp.cmake:33` copia Assets, ProjectSettings y Library, pero omite Engine. `src/renderer/Renderer2D.cpp:45` busca primero un directorio absoluto del build y después recursos portables relativos a SDL.

**Impacto:** el build local puede ocultar la falta usando sus propios archivos; un paquete trasladado a otra máquina carece de la variante del material lit integrado. Se confirmó la omisión del artefacto, no se atribuye una captura visual de otra máquina.

**Corrección:** incluir los recursos internos en la ubicación que resuelve el runtime y verificarlos en packaging. **Regresión:** arrancar/renderizar el paquete trasladado con el árbol del build inaccesible y comprobar material lit con normal/emisión.

## Hallazgos P2

### F13 — Recorridos ECS O(N²)

`src/ecs/EntityManager.hpp:163` llama a `get_component` durante `for_each`; el acceso verifica `is_alive`, implementado con `std::find` sobre todas las entidades (`src/ecs/EntityManager.cpp:168`). Un recorrido donde todas tienen el componente se vuelve cuadrático.

**Medición orientativa:** 10 recorridos con un componente entero, `-O2`, media por recorrido: N=1.000 → 0,309 ms; 5.000 → 8,423 ms; 10.000 → 31,319 ms. Checksum correcto en las tres cargas. Se ejecutó con otras validaciones concurrentes: no es un benchmark aislado ni un presupuesto de juego. La complejidad se deriva del código.

**Corrección/aceptación:** existencia en O(1) y evitar verificaciones redundantes dentro de vistas validadas. Medir de nuevo en Release aislado manteniendo orden, IDs, componentes y resultados.

### F14 — El batch cambia de contrato a partir de 512 sprites

**Evidencia:** `render_probe` (reproducción original). Con máscara de cámara que excluye la capa de los comandos: 511 sprites → 0 renderizados/511 culled; 512 → 512 renderizados/0 culled.

**Ubicación:** `src/renderer/Renderer2D.cpp:743`. El camino paralelo omite la máscara de capas aplicada por `submit_sprite`, y construye una clave antigua de orden por Z/Y/textura en vez de `SpriteSortKey::world`. Se reprodujo la divergencia de culling; la divergencia de sort está comprobada por lectura.

**Corrección/aceptación:** compartir normalización, culling y sort entre caminos; mismos comandos y píxeles para ejecución serial/paralela, incluyendo sorting_layer/order, blend y campos de lighting. Revisar también los campos omitidos por el forwarding del camino serial.

### F15 — La textura blanca se comparte entre renderers sin ownership válido

**Evidencia:** `render_probe`: dos renderers software diferentes reciben el mismo `Texture*` de `Texture::get_white`.

**Ubicación:** `src/renderer/Texture.cpp:92`. Un puntero estático conserva una textura creada para el primer renderer, sin limpieza/recreación. Al cambiar o destruir ese renderer, ya no es un recurso válido para el siguiente.

**Corrección/aceptación:** textura propiedad de cada renderer/cache con limpieza ligada a su vida. Crear dos renderers y recrear uno debe proporcionar recursos propios válidos. El probe confirma la identidad compartida; no fuerza una llamada GPU sobre un recurso ya destruido.

### F16 — Reimportar una dependencia no recupera a sus dependientes

**Evidencia:** `core_probe asset-recover`: tras borrar y reimportar el asset requerido, `owner recovered=0`.

**Ubicación:** `src/core/AssetDatabase.cpp:205`. La reconstrucción sólo degrada registros Ready; los que quedaron MissingDependency no vuelven a evaluarse como candidatos recuperados. Un rescan completo puede ocultar el problema porque restablece estados previamente.

**Corrección/aceptación:** recalcular estados desde existencia/importación real y el grafo, en ambas direcciones. Recuperar cadenas y ciclos al reimportar sin exigir un rescan externo adicional.

### F17 — Una mutación de AssetDatabase que devuelve fallo deja cambios en memoria

**Evidencia:** `core_probe asset-rollback`: impedir guardar la DB y mover registro devuelve `false`, pero el registro viejo desaparece y el nuevo queda presente.

**Ubicación:** `src/core/AssetDatabase.cpp:140`. Se mutan registros/índices antes de `save_database`, sin rollback. `src/editor/commands/AssetFileCommands.cpp:156` revierte el movimiento físico si falla la DB, pero ésta ya conserva el destino en memoria. Import/remove/settings siguen patrones similares.

**Corrección/aceptación:** candidate state con commit tras persistir, o rollback completo; archivo, índices, handles y grafo deben quedar exactamente como antes ante fallo de guardado.

### F18 — JSON sintácticamente válido con tipos incorrectos escapa como excepción

**Evidencia:** `core_probe prefs`: cargar `[]` se acepta, y `set_int` lanza `type_error.305`; `project-type`: `{"version":"bad"}` hace que `ProjectContext::open` lance `type_error.302` en vez de devolver error.

**Ubicación:** `src/core/PlayerPrefs.cpp:122`, `src/core/ProjectContext.cpp:49`. `ProjectSettings::load` y `AssetDatabase::load_database` también usan accesos tipados sin contención completa; esas variantes se identifican por lectura, no por el probe citado.

**Corrección/aceptación:** validar forma/tipos, cargar en estado temporal y mantener defaults/estado anterior ante error. Abrir configuración malformada no debe terminar el editor ni dejar preferencias de un tipo incompatible.

### F19 — SaveSystem ignora la versión del archivo

**Evidencia:** `core_probe save-version`: se cambia sólo la versión del header a 999 y `load_file` devuelve éxito.

**Ubicación:** `src/core/SaveSystem.cpp:119` valida magic, tamaño y CRC, pero no compara versión ni aplica migración. La versión sólo se imprime.

**Corrección/aceptación:** política de versiones soportadas/migración, rechazo de futuras e invalidación segura sin reemplazar el lector previo. El checksum no reemplaza compatibilidad de esquema.

### F20 — IDs altos restaurados permiten asignar la entidad cero

**Evidencia:** `core_probe entity-overflow`: restaurar `UINT32_MAX-1`, crear dos entidades → `4294967295,0`.

**Ubicación:** `src/ecs/EntityManager.cpp:33` y `:62`. El contador puede envolver; el schema sólo exige IDs uint32, no cero y únicos (`src/scene/SceneSchema.cpp:266`), por lo que un archivo editado con IDs altos alcanza el caso sin crear miles de millones de entidades.

**Corrección/aceptación:** reservar cero siempre, gestionar agotamiento/reciclado y overflow de forma explícita. Ninguna entrada admitida debe hacer que `create_entity` devuelva cero o un ID ya vivo como si fuera válido.

### F21 — Quitar collider mediante ECS deja un body fantasma

**Evidencia:** `physics-remove` (reproducción original): `remove_component<ColliderComponent>` seguido de tick deja un body activo.

**Ubicación:** `src/physics/PhysicsSystem.hpp:535`; sólo se limpian entidades muertas. **Límite importante:** los comandos que pasan por `ComponentRegistry::after_remove` sí llaman a `rebuild_body`. El fallo afecta la API ECS directa y rutas que omitan ese finalize; no se afirma que quitarlo desde el inspector reproduzca el caso.

**Corrección/aceptación:** contrato único de lifecycle de componentes o reconciliación de requisitos/generaciones en física. La API pública autorizada para quitar componentes debe eliminar cuerpos/joints derivados.

### F22 — El setter de velocidad angular invierte el resultado

**Evidencia:** `physics-angular` (reproducción original): pedir +90 devuelve -90 tras el siguiente tick.

**Ubicación:** `src/physics/PhysicsSystem.hpp:720`. El setter usa `glm::radians(degrees_per_sec)`, mientras creación/sincronización aplican la convención con signo opuesto.

**Corrección/aceptación:** una conversión angular coherente para create/set/read y cuerpos dynamic/kinematic. Mantener signo y magnitud dentro de tolerancia.

### F24 — El verificador macOS exige un shader propio aunque sea opcional

**Evidencia:** `export` (reproducción original): `string sub-command JSON member 'runtime_shader' not found`.

**Ubicación:** `cmake/VerifyMacOSApp.cmake:14`. ShaderCompilation y StageRuntimeAssets admiten ausencia de runtime_shader, pero verify lo exige incondicionalmente y bloquea `smoke_macos_export`.

**Corrección/aceptación:** verificar shaders del proyecto sólo cuando estén declarados; siempre verificar los internos. Exportar fixtures válidos con y sin shader propio.

### F25 — `assert` elimina validaciones y operaciones de tests en Release

**Evidencia:** inspección de tests y flags CMake. Por ejemplo `tests/EntityManagerTests.cpp:29` y `:33` crean/restauran entidades dentro de `assert`. Release/RelWithDebInfo definen `NDEBUG`, y no se encontró una excepción para esos targets.

**Impacto:** no sólo desaparecen verificaciones; también cambia el flujo del test. No se ejecutó toda la suite Release y no se afirma que todos sus tests pasen o fallen: se confirma que esta estrategia no valida el mismo contrato en esa configuración.

**Corrección/aceptación:** checks siempre activos y setup fuera de asserts, o política explícita de asserts activos en tests; probar deliberadamente un fallo en Debug y Release y verificar que ambos lo detectan.

### F27 — El smoke de runtime rechaza proyectos sin scripts

**Evidencia:** `smoke directo` (reproducción original): escena vacía cargada, pero fallo por no encontrar `libEngineScript_1.dylib`.

**Ubicación:** `src/runtime/StandaloneMain.cpp:98` exige cargar módulo incondicionalmente; `src/enginescript/cli/Main.cpp:84` devuelve éxito sin generar biblioteca si no encuentra `.e2s`. Stage acepta ese resultado.

**Corrección/aceptación:** definir módulo opcional para proyectos sin scripts o generar módulo vacío compatible. Un proyecto válido sin scripts debe pasar smoke; uno que referencia scripts ausentes debe fallar con diagnóstico específico. Este hallazgo es del smoke/contrato de export, no prueba que todo arranque normal sin scripts se cierre.

## Brechas de validación y mejoras, separadas de los bugs

1. **CI:** no hay workflows `.github` en este checkout y el roadmap mantiene CI pendiente. Automatizar build limpio, suite Debug/Release, validación de recursos del paquete y regresiones de estos hallazgos. No se afirma que no exista ningún servicio externo ajeno al repo.
2. **Memoria y concurrencia:** resolver primero el bloqueo local de ASan o usar un runner compatible. Completar ASan/UBSan/TSan sobre runtime y sesiones prolongadas. Los crashes reproducidos no son resultados ASan.
3. **Distribución:** máquina limpia por plataforma, sin fuentes/build/Homebrew accesibles; verificar dependencias transitivas, recursos, módulo nativo, apertura real y ejecución de gameplay. La firma ad-hoc creada localmente no equivale a una distribución firmada/notarizada.
4. **Stress representativo:** medir p95/p99 del frame, memoria estabilizada y tiempos de carga/export con escenas reales después de F13. Los presupuestos de la suite existente y el microbenchmark no certifican la capacidad de un juego.
5. **Sesiones largas de editor:** Save/reabrir, Undo/Redo con fallos, Play/Stop, cambios de escena, hot reload y cancelación de builds durante 100 ciclos. Comprobar estado en disco, dirty y recursos vivos, no sólo que la UI siga abierta.
6. **Separación de responsabilidades:** SceneManager conserva orquestación de runtime junto con Play/editor. Ya existe separación de símbolos del standalone y pruebas; refactorizar donde simplifique lifecycle/ownership, después de cerrar bugs. No hace falta una reescritura general.
7. **Amplitud de features:** posponer nuevas plataformas y herramientas grandes hasta que una vertical slice exportada cumpla las puertas de aceptación. Cooker binario, atlas o Shader Graph pueden ayudar, pero no reemplazan integridad de datos ni son requisitos universales para publicar un juego pequeño.

## Orden propuesto de corrección y puertas de aceptación

| Etapa | Hallazgos | Puerta para avanzar |
|---|---|---|
| 1. Integridad de proyecto | F01, F02, F09, F17, F18, F19 | Fallos de escritura/carga conservan el último estado bueno; ninguna confirmación falsa de guardado. |
| 2. Lifecycle y memoria | F03, F04, F05, F07, F08, F15, F20, F21 | Destroy/spawn/Stop no deja recursos o instancias huérfanas; reentrancia y excepciones no provocan crashes. |
| 3. Gameplay correcto | F06, F11, F12, F14, F16, F22 | Física con jerarquías, límites de geometría, capas y dependencias cumplen las mismas reglas en todos los caminos. |
| 4. Build y distribución | F10, F23, F24, F26, F27 | Build sin caché y paquete autónomo reproducibles, con y sin scripts/shader propio. F10 debe adelantarse si se abren proyectos de terceros. |
| 5. Rendimiento y garantía continua | F13, F25 y CI | Checks activos en Release, coste ECS controlado y pruebas automáticas que mantengan las correcciones anteriores. |

Para considerar este motor productivo en un juego concreto: completar las etapas aplicables, ejecutar una vertical slice de inicio a final en un paquete trasladado y completar una sesión prolongada sin corrupción ni crecimiento sostenido de recursos. La aprobación se limita a plataformas/configuraciones realmente probadas.

## Reproducción y límites

Los probes originales verificaban defectos del commit de referencia y algunos
usaban APIs que ya se retiraron. Las regresiones mantenidas están en `tests/`;
[CORRECCIONES.md](CORRECCIONES.md) relaciona cada hallazgo con sus pruebas y
comandos de validación. Los resultados históricos anteriores no equivalen a una
nueva ejecución ni describen el estado corregido.

La auditoría no incluye revisión línea por línea de dependencias vendorizadas, investigación de CVEs, pruebas de penetración de proveedores, certificación de plataformas o validación visual del proyecto externo. Esas actividades requieren campañas separadas. Ningún resultado de este documento convierte una prueba pendiente en una validación pasada.
