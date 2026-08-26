# Plan de implementación: materiales iluminados con un solo sampler

## Objetivo

Incorporar normal, emissive y specular por sprite sin depender de samplers
adicionales del renderer GPU de SDL 3.4.14. Cada sprite iluminado usará una
textura derivada con sus cuatro mapas, mientras que el shader seguirá leyendo
una única textura. Así se conserva la versión estable de SDL y el camino de
render existente.

Este documento cubre sólo esa solución de materiales realtime. El bake,
composición baked, color lineal y la migración visual de `PlatformerDemo`
pertenecen a incrementos posteriores del plan maestro.

## Estado y decisión

- **DECIDIDO:** empaquetar albedo, normal, emissive y specular en una textura
  derivada 2x2 por región de sprite.
- **DECIDIDO:** conservar SDL 3.4.14; no actualizar a una versión previa ni
  reescribir el renderer nativo sólo para habilitar más samplers.
- **DECIDIDO:** si un pack no puede generarse, el sprite se renderiza por el
  camino actual, sin perder su albedo ni romper la escena.
- **IMPLEMENTADO:** `SpriteLighting2D` tiene handles y resolución runtime para
  normal, emissive y specular, con serialización compatible hacia atrás.
- **IMPLEMENTADO:** generador 2x2, hash O(1) por textura, caché LRU en memoria,
  persistencia validada en `Library/MaterialPacks` y fallback determinista.
- **IMPLEMENTADO:** shader de un sampler, luz dominante, normal/emissive/
  specular, inspector efectivo y variantes MSL, SPIR-V y DXIL empaquetables.
- **A PROBAR:** el número máximo de luces realtime por sprite y el modelo
  final de contribución especular; se elegirá con evidencia GPU, no por una
  estimación teórica.
- **PENDIENTE DEL SIGUIENTE INCREMENTO:** precalentamiento asíncrono. El miss
  inicial se genera en el hilo de render; los hits posteriores son O(1) y el
  caché de disco evita repetir la mezcla entre ejecuciones.

## Contrato del pack `material-pack-v1`

Para una región de albedo de `W x H`, el pack es RGBA8 de `2W x 2H`:

| Cuadrante | Rango de píxeles | Contenido |
| --- | --- | --- |
| superior izquierdo | `[0,W) x [0,H)` | albedo, incluido alfa |
| superior derecho | `[W,2W) x [0,H)` | normal en espacio tangente |
| inferior izquierdo | `[0,W) x [H,2H)` | emissive RGB |
| inferior derecho | `[W,2W) x [H,2H)` | specular RGB |

El shader recibe el UV local de la región, en `[0,1]`. Muestrea albedo en
`uv * 0.5`; normal en `uv * 0.5 + (0.5, 0)`; emissive en
`uv * 0.5 + (0, 0.5)`; y specular en `uv * 0.5 + (0.5, 0.5)`.

Los mapas auxiliares son opcionales y se rellenan en CPU antes de crear el
pack:

- normal ausente: `(128, 128, 255, 255)`;
- emissive ausente: `(0, 0, 0, 255)`;
- specular ausente: `(0, 0, 0, 255)`.

El albedo es obligatorio y conserva su alfa. Las cuatro fuentes deben tener
la misma región útil. La primera entrega exige dimensiones idénticas por
región; no reescala ni reinterpreta UVs silenciosamente. Una posterior mejora
podrá ofrecer remuestreo explícito con filtro y borde definido.

El contrato funciona también con spritesheets: la clave y el pack se generan
por `source_rect`, no por la textura completa. Por ello una animación con
frames distintos no mezcla mapas entre frames.

## Datos, identidad y caché

### Tipos a añadir

En el módulo de render se añadirán tipos equivalentes a:

```cpp
struct MaterialPackSource {
    AssetHandle albedo;
    AssetHandle normal;
    AssetHandle emissive;
    AssetHandle specular;
    SDL_Rect source_rect;
};

struct MaterialPackKey {
    uint32_t format_version; // material-pack-v1
    MaterialPackSource source;
    uint64_t albedo_content_hash;
    uint64_t normal_content_hash;
    uint64_t emissive_content_hash;
    uint64_t specular_content_hash;
};

struct MaterialPackResult {
    std::shared_ptr<Texture> texture;
    MaterialPackKey key;
    SDL_Rect local_albedo_rect;
    enum class Status { Ready, Pending, Unsupported, Failed } status;
};
```

Los nombres definitivos deberán respetar las convenciones existentes, pero el
contrato no cambia: la identidad incluye versión, cuatro fuentes, sus
contenidos y la región solicitada. No se usará sólo el path de archivo, pues
un reimport o una edición con el mismo path debe invalidar el resultado.

### Generación y persistencia

1. `MaterialPackCache` consulta `Texture::pixels()` de las fuentes ya
   cargadas y valida formato, rectángulo y límites.
2. Copia cada región al cuadrante correspondiente y aplica los defaults.
3. En el hilo que posee el renderer crea una `Texture` ordinaria con el buffer
   RGBA resultante; no usa el puntero GPU interno de SDL.
4. Para fuentes estáticas importadas, persiste `Library/MaterialPacks/<key>.rgba`
   y `Library/MaterialPacks/<key>.json`. El manifiesto contiene versión,
   tamaños, hashes, rectángulo y checksum del buffer.
5. La escritura será atómica: archivo temporal en el mismo directorio y
   renombre sólo tras escribir y verificar el checksum.
6. Al iniciar, el caché acepta una entrada de disco únicamente si manifiesto,
   versión, hashes, tamaño y checksum coinciden; en otro caso la descarta y
   la regenera.

La carga/mezcla de píxeles puede realizarse en segundo plano; la creación y
destrucción de recursos del renderer queda en el hilo de render. Durante
`Pending`, se dibuja el albedo normal por el camino existente. Ningún hilo de
fondo puede tocar `SDL_Texture`, `SDL_GPUTexture` ni el backend.

### Invalidación, vida y límites

Se invalida una entrada al cambiar cualquiera de los cuatro assets, su hash
de contenido, su `source_rect`, la versión del algoritmo, el formato o la
configuración de defaults. El AssetDatabase notificará el cambio y el caché
retirará la textura derivada de uso nuevo; comandos ya en vuelo mantienen una
referencia hasta acabar el frame.

Se aplicarán los siguientes límites configurables y testeables:

- `max_material_pack_dimension`, inicialmente conservador y menor o igual que
  la capacidad confirmada por el backend;
- ancho y alto resultantes `2W` y `2H` no pueden exceder ese límite;
- presupuesto de memoria de packs residentes con LRU;
- número máximo de trabajos de generación simultáneos.

Si una imagen de `5000 x 3000` se empaqueta, el resultado sería
`10000 x 6000`; si el límite fuera `8192`, no se genera el pack y se usa el
fallback. El mismo criterio se aplica a mapas que cambian cada frame: se
marcan dinámicos, no se persisten ni se reempaquetan por frame, y permanecen
en el camino actual hasta disponer de un mecanismo específico.

## Integración de escena y render

### Componentes y serialización

`SpriteLighting2D` conservará `normal_map_handle`, `emissive_map_handle` y
`specular_map_handle`; los punteros `Texture*` asociados son sólo runtime y
no se serializan. Las escenas antiguas, sin esos campos, reciben handles
inválidos y por tanto defaults seguros.

La resolución de assets debe suceder tras deserializar, a través de
`AssetDatabase` y `AssetManager`; no se guardan paths ni punteros de textura
en una escena. Las pruebas cubrirán tanto escenas previas como el round-trip
de los tres handles.

### Comando de dibujo

No se modificará el material genérico compartido por todas las entidades. El
`RenderSystem` identifica una entidad que tenga `SpriteComponent` y
`SpriteLighting2D` receptora realtime, solicita el `MaterialPackResult` y
emite un comando específico, por ejemplo `LitSpriteCommand`:

```cpp
struct LitSpriteCommand {
    SpriteCommand base;
    std::shared_ptr<Texture> material_pack;
    MaterialPackUniforms uniforms;
    RealtimeLightingInput lighting;
};
```

`MaterialPackUniforms` incluye offsets/escala de los cuadrantes, color de
tinte, intensidad emissive y parámetros seguros de normal/specular. Así cada
draw conserva sus propios datos y no contamina sprites, UI ni partículas que
comparten materiales.

El backend selecciona el shader de sprite iluminado sólo si `status == Ready`.
Ese shader enlaza el pack en el sampler 0, que es el único sampler que ya
funciona de forma fiable en la ruta SDL estable. Cualquier error de shader,
textura o límite revierte al `SpriteCommand` actual.

### Iluminación realtime

La primera fase del shader toma el albedo, reconstruye la normal de RGB y
suma emissive. La luz se calcula sólo para entidades con
`SpriteLighting2D.receive_realtime == true`; el flag existente de sombras
determina si recibe el factor de sombra que entregue el pipeline realtime.

Para no introducir un contrato implícito, se hará en dos pasos:

1. **Integración inicial:** entrada explícita de la luz dominante ya elegida
   determinísticamente por el sistema de iluminación. Sirve para verificar
   normal, emissive, specular, tintes y fallback de punta a punta.
2. **Integración completa:** buffer acotado de luces por comando o por lote,
   ordenado por contribución y con desempate estable por `EntityId`. El límite
   se configura y se mide. Ambient aporta color base pero no proyecta sombra;
   Point, Spot, Directional y Area aportan sus términos según sus contratos
   realtime existentes.

El orden de composición del plan maestro se preserva: si el sprite es
`baked_receiver`, primero se multiplica su albedo por la lightmap baked;
después se suman las luces realtime, emissive y specular. La implementación de
este plan no habilita por sí misma el compositor baked: deja la interfaz para
conectarlo cuando exista.

## Shader y portabilidad

Se añadirá una variante de shader de sprite iluminado en los formatos que ya
consume el toolchain (HLSL fuente y artefactos/staging necesarios para Metal,
Vulkan y Direct3D). La interfaz será un único sampler más constantes por draw;
no dependerá de semánticas privadas de SDL ni de `SDL_PROP_TEXTURE_GPU_TEXTURE_POINTER`.

El shader tratará el alfa de albedo como autoridad de cobertura. Los mapas
auxiliares no alteran el recorte alfa. Las conversiones sRGB/lineal se
centralizarán en helpers del shader, pero sólo se activarán cuando el futuro
flag de proyecto lineal esté habilitado; el comportamiento actual permanece
idéntico para proyectos existentes.

Antes de exponer el inspector, cada control se etiqueta:

- `normal_map`, `emissive_map`, `specular_map`: realtime, requieren pack
  válido;
- `receive_realtime` y sombras: realtime;
- `baked_receiver`: sin efecto visual hasta que el compositor baked esté
  integrado;
- opciones que no apliquen al tipo de luz: deshabilitadas con explicación,
  no sliders sin efecto.

## Fases de entrega

### Fase 0 — contrato y preparación

- Documentar `material-pack-v1` y añadir tipos/errores sin cambiar draws.
- Completar serialización y resolución segura de los tres mapas.
- Añadir tests de compatibilidad de escena y defaults.

**Salida verificable:** una escena vieja carga igual; una nueva conserva los
tres handles; aún no cambia la imagen renderizada.

### Fase 1 — generador y caché

- Implementar el ensamblador CPU 2x2, validación de rectángulos y defaults.
- Implementar clave, manifiesto, checksum, caché de memoria y persistencia
  atómica bajo `Library/MaterialPacks`.
- Conectar invalidación de reimport y presupuesto LRU.

**Salida verificable:** un test lee píxeles conocidos de los cuatro
cuadrantes; repetir la misma solicitud da cache hit; editar una fuente da una
clave distinta; un tamaño inválido entrega `Unsupported` sin modificar el
sprite original.

### Fase 2 — draw de un sampler y fallback

- Añadir `LitSpriteCommand`, subida de textura derivada en hilo correcto y
  variante de shader de un sampler.
- Enrutar sólo sprites elegibles; mantener el comando ordinario para `Pending`,
  `Unsupported` o `Failed`.
- Compilar, stagear y empaquetar los shaders para los backends admitidos.

**Salida verificable:** smoke GPU Metal con un pack artificial que da píxeles
distintos para albedo, normal, emissive y specular; una textura imposible
produce el mismo resultado que el fallback de sprite común.

### Fase 3 — entrada realtime y controles

- Conectar la luz dominante determinística y los flags de
  `SpriteLighting2D`.
- Aplicar normal, emissive y specular con defaults, rangos documentados y
  controles del inspector efectivos.
- Añadir la ruta de múltiples luces acotada sólo tras aprobar la medición de
  la fase inicial.

**Salida verificable:** una escena de prueba demuestra que normal/emissive/
specular alteran solamente sprites receptores; al quitar el componente el
resultado vuelve al sprite normal.

### Fase 4 — bake, lineal y demo (plan maestro posterior)

- Conectar el punto de multiplicación `baked_receiver` al compositor baked.
- Activar la ruta lineal protegida por flag de proyecto y ajustar sólo
  `PlatformerDemo` en el repositorio externo, después de aprobar el diff de
  motor.
- Verificar bake automático, export con bake y Play visual de la demo.

**Salida verificable:** el albedo del entorno receptor se multiplica por el
bake y el personaje recibe las luces realtime posteriormente, sin migrar otros
proyectos.

## Matriz de pruebas y criterios de aceptación

| Área | Prueba | Criterio |
| --- | --- | --- |
| layout | píxeles de 1x1, 2x2 y `source_rect` | cada cuadrante coincide exactamente |
| defaults | mapas opcionales ausentes | normal plano, emissive/specular negros |
| límites | resultado `2W x 2H` supera máximo | `Unsupported` y fallback intacto |
| caché | mismo contenido, cambio de contenido y manifiesto corrupto | hit estable, invalidación/regeneración correcta |
| serialización | escena antigua y round-trip moderno | sin pérdida ni crash |
| render | selección de comando | sólo sprites elegibles usan el shader lit |
| GPU | Metal y fallback disponible | color esperado con un sampler; error degrada con seguridad |
| shaders | compilación y staging de cada backend | artefactos encontrados por runtime/export |
| regresión | CTest completo y C++ generado de proyecto representativo | sin regresiones |
| visual | escena de prueba y luego `PlatformerDemo` | evidencia separada de runtime, no sólo estructural |

Además de `ctest --output-on-failure` y `git diff --check`, cada fase de render
ejecutará el smoke GPU Metal cuando haya hardware disponible y conservará una
prueba de fallback. La validación de export se realiza sobre una copia de
proyecto representativa antes de tocar el proyecto externo.

## Coste y medición

El empaquetado ocurre al importar, cambiar una fuente o fallar una entrada de
caché; no debe ejecutarse cada frame. Cada pack ocupa `4 x W x H x 4` bytes
en RGBA8, frente a `W x H x 4` bytes de un albedo aislado. El coste real de
memoria depende de si las fuentes auxiliares y el albedo original permanecen
residentes, por lo que no se declarará un porcentaje de ahorro ni de coste sin
medición.

En GPU el shader hace cuatro lecturas de la misma textura derivada en vez de
una lectura de albedo, pero no añade una segunda vinculación de sampler. Se
registrarán: tiempo de construcción, hits/misses, bytes residentes, evicciones,
tiempo de CPU de frame y tiempo GPU de la escena de prueba. Los resultados se
compararán contra el mismo escenario con fallback, en el mismo backend y
resolución.

## Exclusiones explícitas

- No reempaquetar texturas animadas o modificadas cada frame.
- No hacer una migración masiva de sprites o proyectos existentes.
- No usar APIs internas/privadas de SDL ni requerir una SDL no estable.
- No prometer sombreado físicamente correcto de todas las luces antes de la
  fase de buffer de luces y su prueba GPU.
- No commitear automáticamente: antes de cada commit se presentarán diff,
  verificaciones y resultado visible para aprobación explícita.
