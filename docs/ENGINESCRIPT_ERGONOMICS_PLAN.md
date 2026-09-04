# Plan de seguridad y ergonomía de EngineScript

## Objetivo

Hacer más cómoda la programación de juegos sin ocultar costos, perder seguridad
estática ni romper scripts válidos. Los incrementos se entregan de forma
independiente y se validan tanto con C++ generado como con el proyecto de
referencia.

El análisis actual del proyecto encontró conversiones y concatenaciones de
texto repetidas, comprobaciones nullable verbosas, estados representados con
números, arrays paralelos usados como colas, búsquedas reiteradas de entidades y
copias artificiales destinadas a evitar aliasing de proxies de componentes.

## Incrementos

### 0. Vistas de componentes seguras

- Reemplazar el proxy único reutilizado por tipo por vistas estables por entidad
  durante cada callback.
- Sincronizar todas las vistas consultadas al finalizar el callback y resolver
  nuevamente los `ComponentRef<T>` conservados al comenzar el siguiente.
- Cubrir lecturas y escrituras simultáneas sobre varias entidades, componentes
  destruidos y callbacks consecutivos.
- Eliminar la necesidad de materializar valores mediante expresiones como
  `position + Vec2.zero()`.

### 1. Interpolación y conversión de texto

- Incorporar backticks multilínea con expresiones `${value}` y escapes para
  backticks e interpolaciones literales.
- Evaluar cada expresión una sola vez, de izquierda a derecha, y construir el
  resultado mediante un builder interno.
- Admitir conversiones deterministas y sin locale para `String`, números,
  `Bool`, `Duration`, enums y tipos matemáticos nativos documentados.
- Admitir tipos propios que expongan un método público, síncrono, sin parámetros
  y con firma `toString(): String`; no exigir `implements Stringable`.
- Incluir formato numérico básico validado por tipo, como `${seconds:02}` y
  `${damage:.2f}`.
- Mantener `String(value)` y strings entre comillas. La suma entre `String` y un
  valor no-string continúa siendo inválida.

### 2. Opcionales y expresiones condicionales

- Incorporar `if (let value = optional)` para limitar el valor desempaquetado al
  bloque positivo.
- Incorporar `guard (let value = optional) else { ... }`; el bloque `else` debe
  terminar el flujo mediante `return`, `break` o `continue`, y el valor queda
  disponible después del guard.
- Evaluar la expresión nullable una sola vez y conservar el narrowing actual de
  los chequeos explícitos contra `null`.
- Añadir el condicional lazy `condition ? trueValue : falseValue`, con tipos de
  ramas compatibles.

### 3. Estados, enums y `match`

- Completar la cobertura end-to-end de enums antes de utilizarlos como contrato
  principal para estados de gameplay.
- Permitir enums en propiedades `@expose`, editarlos como dropdown y serializar
  el nombre del miembro, nunca su ordinal.
- Conservar y señalar como inválido un miembro serializado que ya no exista; no
  sustituirlo silenciosamente.
- Incorporar `match` sin fall-through para statements y expresiones.
- Soportar miembros de enum, `Bool`, literales numéricos y string, `null` y `_`.
  Exigir exhaustividad para enums, booleanos y nullables; los dominios abiertos
  requieren `_`.
- Dejar el destructuring de `Result` y la propagación abreviada de errores para
  un incremento posterior.

### 4. Argumentos por defecto y nombrados

- Completar el soporte ya iniciado para defaults en funciones, métodos,
  constructores y APIs del catálogo.
- Limitar defaults a constantes, `null`, miembros de enum y constructores de
  valores constantes, y exigir que sean parámetros finales.
- Admitir argumentos nombrados después de los posicionales, reordenarlos según
  la firma antes de generar IR y rechazar nombres desconocidos, duplicados u
  omisiones sin default.

### 5. Colecciones y consultas de gameplay

- Añadir `Deque<T>` determinista con longitud, consulta de vacío, inserción y
  extracción por ambos extremos; lecturas o extracciones vacías devuelven `T?`.
- Documentar el reemplazo de arrays paralelos por una colección de structs para
  impedir que los datos relacionados se desincronicen.
- Añadir `entities.firstByGameplayTag(tag): Entity?` y conservar
  `findByGameplayTag` por compatibilidad.
- Posponer queries cacheadas y transacciones de prefab hasta contar con
  mediciones específicas.

## Documentación y auditoría para IA

La documentación publicada describe siempre la implementación vigente. No se
mantienen snapshots ni documentos separados por versión; los cambios históricos
permanecen en Git.

Cada incremento debe:

- actualizar la especificación del lenguaje, el contrato de tipos, los ejemplos
  canónicos y los anti-patrones;
- generar la referencia de APIs desde el catálogo usado por compilador y LSP;
- actualizar autocompletado, hover, formatter, semantic tokens y code actions;
- exponer mediante `e2sc --check --json` diagnósticos con código, severidad,
  archivo, rango, explicación y corrección sugerida;
- exponer datos machine-readable de las capacidades vigentes del lenguaje, ABI
  y SDK para que una IA no deba inferirlas;
- añadir fixtures válidos e inválidos utilizables como pruebas y como ejemplos;
- exigir que una auditoría separe errores, riesgos de rendimiento y sugerencias
  opcionales.

La guía de auditoría para IA debe revisar compilación y tipos, nullabilidad,
lifecycle, estado transitorio, búsquedas y asignaciones por frame, componentes,
assets, determinismo, hot reload y costos potenciales de memoria o CPU.

## Compatibilidad y validación

- Los patrones existentes (`String(value)`, chequeos contra `null`, `if/else` y
  bucles clásicos) continúan siendo válidos; no hay migración automática.
- Cada incremento cubre lexer, parser, símbolos, type checker, IR, codegen, LSP,
  diagnósticos y recuperación ante código incompleto según corresponda.
- Se compila y ejecuta C++ generado representativo y se ejecutan las suites de
  EngineScript, Play Mode, hot reload y serialización aplicables.
- Los scripts reales del proyecto de referencia se validan con `e2sc --check` y
  Play sin modificarlos como parte del trabajo del motor.
- El identificador de caché cambia cuando cambia el codegen; ABI y SDK cambian
  sólo cuando se modifica el contrato nativo público.
