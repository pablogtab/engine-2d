# Plan de biblioteca estándar de EngineScript

**Estado:** propuesta ejecutable. Ámbito: Engine2D y EngineScript; sin gameplay, assets ni dependencias de proyectos externos. Complementa fase 11 de `ROADMAP.md` y `docs/ENGINE_SCRIPT_LANGUAGE_ROADMAP.md`.

## Objetivo

Crear una base chica, coherente y reproducible para matemática, vectores, aleatoriedad, interpolación, geometría y colecciones. No busca replicar C# o C++: evita helpers repetidos sin inflar ABI ni acoplar la biblioteca a ECS, renderer, escena o servicios.

Experiencia buscada (a confirmar mediante spike):

```es
var rng = Random(42u);
var target = Vec2.lerp(a, b, Ease.inOutSine(progress));
position = target + Random.insideUnitCircle(rng) * Math.clamp(speed, 0.0, 20.0);
```

## Decisiones

| Estado | Decisión | Efecto |
| --- | --- | --- |
| DECIDIDO | El catálogo declara API antes que runtime. | Compilador, LSP y export comparten contrato. |
| DECIDIDO | Math, Ease y vectoriales son puras. | Sin estado global; pruebas por propiedades. |
| DECIDIDO | Random es explícito y sembrado. | Replays y tests reproducibles. |
| DECIDIDO | ABI, SDK, cache y catálogo cambian juntos. | No hay C++ generado incompatible. |
| A PROBAR | `Math.clamp(x,a,b)` y `Vec2.length(v)`. | El spike valida estáticos. |
| A PROBAR | Random muta sin copia silenciosa. | El backend preserva estado. |
| IDEA | `EngineKit/Timer`, Grid y Steering. | Fuera del motor hasta tener paquetes. |
| DESCARTADO | Física, UI, audio, IO, red, reflection y tween lifecycle. | Son servicios o gameplay. |

## Arquitectura

1. `src/enginescript/api/engine_api.json` es fuente de verdad de tipos, firmas, docs y disponibilidad.
2. Resolver, type checker, IR, emitter y LSP consumen el catálogo.
3. Headers C++ implementan las firmas y se incluyen desde el módulo generado.
4. Un test exige que catálogo, `LanguageVersion.hpp` y artefactos de `e2sc`/LSP tengan la misma identidad.

| Área | Ubicación propuesta |
| --- | --- |
| Contrato | `src/enginescript/api/engine_api.json` |
| Runtime | `src/enginescript/runtime/{Math,VectorOps,Random,Ease,Geometry}.hpp` |
| Colecciones | `src/enginescript/runtime/CollectionAlgorithms.hpp` |
| Integración | `GameModule.hpp` y/o include del emitter |
| Pruebas | `tests/EngineScript*Tests.cpp` y fixtures |
| Docs | `docs/SCRIPTING.md` y este documento |

El runtime es header-only y no depende de ECS, SDL, escena ni mundo.

## Convenciones

- Float inicial: `Float` (Float32); no se multiplican overloads sin casos y pruebas.
- Ángulos en radianes: `degreesToRadians` y `radiansToDegrees` son explícitos.
- `lerp` no clampa; `lerpClamped` y `Ease` usan `[0,1]`.
- RNG e intervalos enteros usan `[minInclusive,maxExclusive)`.
- Límites invertidos se normalizan; intervalos colapsados devuelven el límite.
- Se preserva IEEE para `NaN` e infinito salvo contrato explícito.
- Tipos PascalCase; miembros camelCase.

## API beta

### Math

Tipo estático sin estado con `PI`, `TAU`, `EPSILON`.

| Grupo | Funciones | Regla |
| --- | --- | --- |
| Básicas | `abs`, `sign`, `min`, `max`, `clamp`, `saturate` | `sign(0)=0`; clamp normaliza. |
| Redondeo | `floor`, `ceil`, `round`, `trunc`, `frac` | Spike fija empates de round. |
| Potencias | `sqrt`, `pow`, `exp`, `log`, `log10` | IEEE fuera del dominio. |
| Trigonometría | `sin`, `cos`, `tan`, `asin`, `acos`, `atan2` | Radianes. |
| Interpolación | `lerp`, `lerpClamped`, `inverseLerp`, `remap`, `smoothStep`, `repeat`, `pingPong` | inverseLerp degenerado retorna 0. |
| Comparación | `approximately(a,b,epsilon=EPSILON)` | Absoluta y relativa. |
| Conversión | `degreesToRadians`, `radiansToDegrees` | Unidades explícitas. |

`approximately`: `abs(a-b) <= epsilon * max(1,abs(a),abs(b))`. `remap` con origen colapsado retorna destino mínimo.

### Vectores y Color

| Tipo | API inicial |
| --- | --- |
| Vec2 | `zero`, `one`, direcciones, `length`, `lengthSquared`, `normalized`, `distance`, `dot`, `cross`, `lerp`, `moveTowards`, `reflect`, `project`, `angle`, `signedAngle`, `fromAngle`, `clampMagnitude`. |
| Vec3 | `zero`, `one`, `length`, `lengthSquared`, `normalized`, `distance`, `dot`, `cross`, `lerp`, `project`, `reflect`, `clampMagnitude`. |
| Vec4 | `zero`, `one`, `length`, `normalized`, `dot`, `lerp`. |
| Color | `black`, `white`, `clear`, `lerp`, `withAlpha`; lineal/sRGB sólo si renderer lo garantiza. |

Normalizar cero devuelve cero; ángulos son radianes; `moveTowards` no sobrepasa destino. Si el catálogo no representa constantes struct estables, se usa `Vec2.zero()`.

### Random

Tipo de valor explícito: sin singleton, reloj ni `std::rand`.

| API | Resultado |
| --- | --- |
| `Random(seed: UInt64)` | Estado reproducible; seed 0 válida. |
| `nextUInt(rng)` | UInt32 uniforme. |
| `nextFloat01(rng)` | Float `[0,1)`. |
| `rangeInt(rng,min,max)` | Int `[min,max)`. |
| `rangeFloat(rng,min,max)` | Float `[min,max)`. |
| `chance(rng,p)` | Falso para `p<=0`; verdadero para `p>=1`. |
| `insideUnitCircle(rng)`, `onUnitCircle(rng)` | Vec2 dentro/sobre círculo. |
| `fork(rng,stream)` | Stream independiente determinista. |

Propuesta: PCG32, fijado con vectores dorados. Si no se puede mutar por referencia, se retorna el estado nuevo; nunca hay copia muda.

### Ease, Rect y colecciones

- Ease: linear e in/out/inOut de Quad, Cubic, Sine, Expo y Back. Clampa `t`, extremos 0 y 1. Bounce, Elastic y unclamped se postergan.
- Rect: `{position: Vec2,size: Vec2}` con `normalized`, `fromMinMax`, `min`, `max`, `center`, `contains`, `overlaps`, `intersection: Option<Rect>`, `union`, `closestPoint`, `expand`. Contains incluye borde; overlaps exige área positiva.
- Array: `contains`, `indexOf`, `lastIndexOf`, `clear`, `removeAt`, `removeValue`, `reverse`, `swap`.
- Map/Set: básicos sin asignaciones inesperadas. `shuffle`/`choose` reciben Random explícito. `map`, `filter`, `reduce`, comparadores e iteradores lazy esperan spike de closures, genéricos y ownership.

## Fases

### 0. Auditoría y spike

1. Comparar `engine_api.json` y `LanguageVersion.hpp`; bloquear divergencia de cache ID, ABI y SDK hash.
2. Trazar `Math.clamp` por parser, resolver, checker, IR y C++ emitter.
3. Trazar Random mutable y confirmar que no se copia.
4. Elegir lowering: static struct, namespace generado o adaptación del emitter.
5. Probar identidad del catálogo junto a `e2sc` y LSP.

Salida: C++ generado compilable y ADR corta con sintaxis confirmada.

### 1. Fundación

Crear include común, patrón de headers, fixtures de fuente/IR/C++ y documentación de símbolos. Integrar una sola función Math punta a punta antes de crecer API.

### 2. Math

Implementar básicos, interpolación, trigonometría y conversiones. Probar cero, límites invertidos, no finitos y round; sumar ejemplo de movimiento.

### 3. Vectores y Color

Completar Vec2 con ejemplo de steering/movimiento. Agregar Vec3/Vec4 sin rutas especiales. Color sólo con espacio explícito y evidencia renderer.

### 4. Random determinista

Runtime independiente de plataforma, mínimo 20 vectores dorados, tests de intervalos y ejemplo reproducible. Verificar hot reload, play y export.

### 5. Ease y Rect

Probar extremos y monotonicidad; probar rects invertidos, tamaño cero y bordes. Bounds2 sólo con consumidor real.

### 6. Colecciones conservadoras

Inventariar costos; agregar sólo operaciones sin closures. Callbacks y algoritmos avanzados van a spike separado y se miden antes de afirmar rendimiento.

### 7. LSP, documentación y beta

Actualizar `docs/SCRIPTING.md`; verificar completion, hover, navegación y diagnósticos. Crear proyecto externo mínimo que compile y ejecute las familias beta. Publicar versión y deprecación.

## Validación obligatoria

| Capa | Evidencia |
| --- | --- |
| Catálogo | JSON, firmas, docs e igualdad ABI/SDK/cache. |
| Frontend | Lexer, parser, resolver, checker y conversiones. |
| Backend | IR y C++ generado esperado por familia. |
| Compilación | C++ generado contra runtime real. |
| Herramientas | `e2sc`, LSP y catálogo junto a binarios. |
| Integración | `e2sc --check` de proyecto representativo. |
| Runtime | Play mode y export para beta. |
| Determinismo | Vectores dorados, límites, ceros y no finitos. |
| Calidad | Suites enfocada/completa y `git diff --check`. |

No se acepta firma sin runtime, runtime sin catálogo, ni evidencia sólo headless para cerrar beta.

## Compatibilidad y riesgos

- Runtime expuesto cambia ABI/SDK/cache atómicamente, tomando valor del checkout actual.
- Cambio de lowering, semántica o runtime invalida caché; docs aisladas no.
- Random no cambia algoritmo ni intervalos en una versión menor; se versiona como tipo nuevo si fuera necesario.
- El spike reduce riesgo de estáticos; fixtures de secuencia detectan copia RNG; pruebas de propiedades fijan geometría/easing; scope conservador evita asignaciones y explosión de templates.
- Helpers con entidades, escena, tiempo implícito o lifecycle permanecen en paquetes externos hasta tener distribución.

## Commits y definición de terminado

1. `test(enginescript): align catalog identity checks`
2. `feat(enginescript): add standard library runtime foundation`
3. `feat(enginescript): add math library`
4. `feat(enginescript): add vector operations`
5. `feat(enginescript): add deterministic random`
6. `feat(enginescript): add easing and rect utilities`
7. `feat(enginescript): extend core collections`
8. `docs(enginescript): publish standard library beta`

Cada módulo termina sólo si catálogo, runtime y docs coinciden; casos felices y borde pasan desde fuente hasta C++ generado; proyecto real compila; LSP lo ve cuando aplica; versiones coherentes; y pasan `git diff --check` y las suites acordadas.

## Próxima acción

Ejecutar fase 0 con `Math.clamp(Float, Float, Float)`. Debe demostrar que estáticos resuelven y emiten sin fragilidad, módulo generado ve runtime correcto y catálogo/ABI/SDK/cache están sincronizados por tests.
