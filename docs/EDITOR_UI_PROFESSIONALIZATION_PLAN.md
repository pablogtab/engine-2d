# Plan de profesionalizacion de la UI del editor

Estado: `PLANIFICADO`

## 1. Objetivo

Llevar la interfaz del editor de Engine2D a un acabado profesional inspirado en
la organizacion de Blender, conservando el editor utilizable durante todo el
proceso y sin introducir de entrada una migracion completa a otro toolkit.

La estrategia inicial es construir un sistema visual y una arquitectura de
editor propios sobre Dear ImGui. Una compuerta de decision, despues de una
vertical funcional completa, determinara con evidencia si ImGui permite
alcanzar el resultado esperado o si la capa de presentacion debe migrarse a
Qt/QML.

El plan no incluye codigo, recursos ni comportamiento especificos de un juego.

## 2. Decision arquitectonica

No se reemplazara ImGui antes de comprobar sus limites con una implementacion
representativa. Tampoco se intentara ocultar todas las llamadas a ImGui detras
de una interfaz universal de widgets.

Se separaran tres responsabilidades:

1. Estado y operaciones del editor: escenas, seleccion, comandos, assets,
   sesiones, Play Mode y persistencia.
2. Modelos de presentacion y acciones: informacion que consume cada panel y
   comandos que puede ejecutar.
3. Presentacion: sistema visual y componentes de Engine2D implementados
   inicialmente con ImGui.

Esta separacion permite mejorar la UI ahora y, si fuera necesario, reemplazar
solamente la presentacion en el futuro.

## 3. Situacion inicial

El editor ya dispone de una base funcional que debe preservarse:

- C++20, SDL3 y Dear ImGui con docking.
- Paneles modulares mediante `EditorPanel`.
- Undo/redo y transacciones de propiedades.
- Seleccion y estado por escena.
- Autosave, recuperacion y deteccion de cambios externos.
- Layout y visibilidad de paneles persistentes.
- Inspector con controles reutilizables y valores mixtos.
- Iconografia Lucide integrada.
- Viewport renderizado a una textura SDL.
- Herramientas de escena, prefabs, assets, animacion, particulas e IA.

Los principales puntos a corregir son:

- Miles de llamadas directas a `ImGui::` distribuidas por los paneles.
- Tema global basado en colores y dimensiones sin semantica de producto.
- Aspecto visible de widgets ImGui predeterminados.
- Atajos publicamente acoplados a `ImGuiKey`.
- Distribucion de contexto a paneles mediante conocimiento de tipos concretos.
- Ausencia de una anatomia comun para headers, toolbars, regiones y sidebars.
- Falta de una validacion visual sistematica del editor completo.

## 4. Arquitectura objetivo

La organizacion se inspira en los conceptos de workspace, area, region y
operador de Blender, sin copiar su codigo ni su identidad visual.

```text
EditorApplication
|-- EditorActionRegistry
|-- EditorSessionContext
|-- EditorPanelRegistry
|-- WorkspaceManager
|   |-- 2D
|   |-- Animation
|   |-- Tilemap
|   |-- VFX
|   `-- Debug
|-- EditorArea
|   |-- Header
|   |-- Toolbar
|   |-- Main region
|   |-- Sidebar
|   `-- Footer
`-- UI backend
    |-- Engine2D UI Kit
    `-- ImGui implementation
```

Estructura de codigo propuesta:

```text
src/editor/
|-- app/
|   |-- EditorContext
|   |-- EditorActionRegistry
|   `-- EditorPanelRegistry
|-- workspace/
|   |-- EditorWorkspace
|   |-- EditorArea
|   `-- WorkspaceManager
|-- ui/
|   |-- EditorUiTheme
|   |-- EditorUiMetrics
|   |-- EditorUiPalette
|   |-- EditorUiFonts
|   `-- widgets/
|       |-- AreaHeader
|       |-- Toolbar
|       |-- SearchField
|       |-- PropertyGrid
|       |-- TreeView
|       |-- Tabs
|       |-- Breadcrumb
|       |-- Toast
|       `-- Modal
|-- panels/
`-- viewmodels/
```

Los nombres son orientativos. Cada incorporacion debe justificar una frontera
real y evitar jerarquias o abstracciones que no tengan mas de un consumidor.

## 5. Principios de UX

- Identidad visual propia de Engine2D, no una replica grafica de Blender.
- Densidad compacta y jerarquia clara.
- Una anatomia comun para todos los editores.
- Acciones consistentes entre menus, toolbars, atajos y command palette.
- Contexto visible: workspace, escena, seleccion, Play Mode y estado de guardado.
- Estados vacios, loading, error y disabled diseñados explicitamente.
- Flujo normal de layout; evitar coordenadas absolutas y scrolls anidados salvo
  que la interaccion lo requiera.
- El viewport y la edicion deben seguir sintiendose inmediatos.
- Compilar correctamente no es evidencia de aceptacion visual.

## 6. Fases

### Fase 0 - Linea base y direccion visual

Duracion estimada: 2 a 3 dias.

#### Trabajo

1. Capturar el editor actual en 1440x900, 1920x1080 y Retina/macOS.
2. Capturar ventanas angostas, layout predeterminado y layout personalizado.
3. Registrar los recorridos funcionales criticos:
   - abrir un proyecto y una escena;
   - seleccionar y editar una entidad;
   - arrastrar un asset a la escena;
   - iniciar, pausar y detener Play Mode;
   - modificar, deshacer, guardar y restaurar el layout.
4. Definir una referencia visual del workspace 2D.
5. Especificar tipografias, densidad, paleta, iconos, alturas y espaciados.

#### Entregables

- Capturas de linea base.
- Referencia visual aprobada.
- Matriz de recorridos funcionales.
- Checklist visual verificable.

#### Criterio de salida

Debe existir una definicion observable de que significa "profesional" para el
editor. No se inicia una reescritura basada unicamente en preferencias
subjetivas sin una referencia comun.

### Fase 1 - Sistema visual sobre ImGui

Duracion estimada: 1 semana.

#### Trabajo

1. Reemplazar el tema monolitico por tokens semanticos:

   ```text
   surface.window
   surface.panel
   surface.header
   text.primary
   text.secondary
   action.primary
   state.hovered
   state.selected
   status.success
   status.warning
   status.error
   ```

2. Incorporar una tipografia principal y una monoespaciada con licencias
   compatibles y distribucion controlada bajo assets del editor.
3. Mantener Lucide y ampliar el catalogo mediante nombres semanticos.
4. Centralizar el escalado DPI de fuentes, iconos, paddings y alturas.
5. Crear helpers RAII para estilos, colores, IDs y estados disabled.
6. Construir los primeros componentes propios:
   - botones primario, secundario e icon-only;
   - campos de texto y busqueda;
   - toggle y checkbox;
   - combo y selector de assets;
   - headers de seccion;
   - tabs;
   - separadores y badges de estado.
7. Crear una galeria interna de componentes.

#### Entregables

- `EditorUiPalette`, `EditorUiMetrics`, `EditorUiFonts` y tema central.
- Componentes base documentados.
- Galeria con estados normal, hover, active, focused, disabled, selected,
  mixed y error.

#### Criterio de salida

Ningun componente nuevo debe introducir colores, paddings o dimensiones
visuales dispersos sin pasar por el sistema central.

### Fase 2 - Desacoplamiento estructural

Duracion estimada: 1 a 2 semanas.

#### Acciones y atajos

1. Crear `EditorActionId` y `EditorActionRegistry`.
2. Representar teclas mediante tipos del editor, no `ImGuiKey` en la API
   publica.
3. Traducir SDL e ImGui dentro del backend de input.
4. Hacer que menus, toolbars, atajos y command palette ejecuten las mismas
   acciones.
5. Exponer nombre, categoria, habilitacion, check state y shortcut de cada
   accion.

#### Contexto

Crear un `EditorContext` que exponga de forma controlada:

- escena y sesion activas;
- seleccion;
- historial de comandos;
- estado de entidades;
- servicios de assets;
- Play Mode;
- navegacion de escenas y prefabs.

Los paneles recibiran cambios de contexto mediante un contrato comun. Se
eliminara gradualmente el conocimiento de tipos concretos y los
`dynamic_pointer_cast` usados para actualizar sesiones.

#### Identidad y modelos

1. Crear identificadores estables de panel separados del titulo visible.
2. Separar modelos de presentacion para Hierarchy, Inspector, Asset Browser y
   Scene Tabs.
3. Mantener mutaciones dentro del sistema de comandos existente.
4. Probar modelos y acciones sin requerir un frame de ImGui.

#### Criterio de salida

La logica de una accion critica debe poder probarse sin dibujar la UI. Cambiar
el titulo visible de un panel no debe romper su estado persistido.

### Fase 3 - Shell profesional del editor

Duracion estimada: 1 a 2 semanas.

#### Trabajo

1. Crear una topbar con selector de workspace.
2. Dar a cada area un header consistente con tipo de editor y acciones.
3. Incorporar una status bar permanente.
4. Mostrar escena activa, estado dirty, autosave, Play Mode y tareas en curso.
5. Crear una command palette basada en `EditorActionRegistry`.
6. Organizar el menu de paneles y ventanas.
7. Permitir restaurar el layout del workspace actual.
8. Versionar el formato de layouts y tolerar layouts invalidos.

#### Workspaces iniciales

- `2D`: Hierarchy, Viewport, Inspector y Assets.
- `Animation`: Viewport, Animation Studio e Inspector.
- `Tilemap`: Viewport, Palette e Inspector.
- `VFX`: Viewport, Particles y Post Process.
- `Debug`: Viewport, Console, Diagnostics y Metrics.

ImGui seguira resolviendo docking y ventanas, pero el usuario vera areas del
editor con anatomia Engine2D y no ventanas genericas de ImGui.

#### Criterio de salida

Cambiar de workspace, modificar el docking, restaurar defaults y reiniciar el
editor debe conservar un estado coherente y recuperable.

### Fase 4 - Vertical profesional completa

Duracion estimada: 2 a 3 semanas.

Esta fase valida si ImGui puede alcanzar el resultado deseado. Se migran juntas
las cuatro superficies que forman el flujo principal.

#### Hierarchy

- Filas compactas y estados seleccionados claros.
- Sangria, iconos por tipo, visibilidad y bloqueo.
- Renombrado, seleccion multiple, drag and drop y menus contextuales.
- Busqueda con contador, resaltado y estado vacio.
- Feedback valido e invalido al reparentar.

#### Viewport

- Header de escenas y prefab stage.
- Toolbar de transformacion consistente.
- Play, Pause y Stop integrados.
- Overlays agrupados por funcion.
- Breadcrumbs y menus contextuales.
- Foco, snap, grid y espacio local/global claramente visibles.
- El input del juego conserva prioridad durante Play Mode.

#### Inspector

- Header de entidad y estado runtime/editor.
- Property grid consistente.
- Componentes con headers propios.
- Edicion simple y multiple.
- Valores mixtos, reset, revert y validacion inline.
- Asset pickers y drag and drop.
- Interaccion scrubby y transacciones de propiedades preservadas.

#### Asset Browser

- Header, toolbar y breadcrumbs.
- Busqueda, filtros y navegacion.
- Modos grid y lista.
- Thumbnails y seleccion consistentes.
- Estados de importacion y errores.
- Menus contextuales y drag and drop.

#### Criterio de salida

En estas cuatro superficies no deben quedar controles centrales con apariencia
predeterminada de ImGui. Todos los recorridos de la fase 0 deben continuar
funcionando y el resultado debe validarse mediante capturas reales.

### Compuerta de decision - ImGui o Qt/QML

Duracion estimada: 1 a 2 dias para evaluar. Un spike, si se activa, tendra un
limite de 2 semanas.

#### Continuar con ImGui si

- El acabado visual alcanza la referencia aprobada.
- Docking, DPI, tipografias y ventanas funcionan correctamente.
- Los componentes propios son mantenibles.
- Las interacciones complejas no exigen hacks repetidos.
- El rendimiento del editor y el viewport se mantiene estable.

#### Ejecutar un spike Qt/QML si

- Persisten limites materiales de layout o composicion.
- Texto complejo, IME o accesibilidad resultan insuficientes.
- El escalado DPI no puede estabilizarse.
- Las ventanas nativas presentan problemas relevantes.
- Mantener los widgets personalizados cuesta mas que una segunda
  implementacion de presentacion.

#### Alcance maximo del spike Qt/QML

- Ventana principal y docking.
- Hierarchy minimo.
- Inspector minimo.
- Viewport SDL/GPU embebido.
- Seleccion sincronizada.
- Una propiedad editable con undo/redo.
- Aplicacion macOS empaquetada.

No se aprueba la migracion completa sin demostrar integracion del viewport,
input correcto, persistencia y empaquetado. Electron queda fuera del camino
principal por el costo de integrar procesos, IPC y viewport.

### Fase 5 - Paneles principales

Duracion estimada: 3 a 5 semanas.

Orden sugerido:

1. Scene Tabs y Prefab Stage.
2. Console.
3. Project Settings.
4. AI Assistant.
5. Scene Diagnostics.
6. Stats y Metrics.
7. Shortcuts Preferences.

Los paneles grandes se migraran por secciones pequeñas. Cada seccion debe
mantener comportamiento, estado, undo/redo y persistencia antes de retirar la
presentacion anterior.

### Fase 6 - Editores especializados

Duracion estimada: 2 a 4 semanas.

- Sprite Animation Studio.
- Tilemap Palette.
- Particle Effect Editor.
- Post Process Tuner.
- Project Hub.
- Modales de recuperacion, conflictos y guardado.

Cada editor usara la misma anatomia:

```text
Header
|-- Mode
|-- Menus
`-- Display options

Main region
|-- Toolbar
|-- Canvas or content
`-- Optional sidebar

Footer
`-- Contextual status
```

### Fase 7 - Terminacion y endurecimiento

Duracion estimada: 1 a 2 semanas.

#### UX

- Navegacion completa por teclado.
- Foco visible y predecible.
- Tooltips, truncado y textos largos.
- Estados vacios, loading, error y offline.
- Consistencia de menus contextuales y shortcuts.
- Preparacion para localizacion, aunque inicialmente permanezca en ingles.

#### Plataforma

- DPI 100%, 150% y 200%.
- Retina/macOS.
- Ventanas pequeñas y monitores multiples.
- Restauracion segura ante layouts corruptos.
- Empaquetado y ejecucion desde la aplicacion instalada.

#### Rendimiento

- Medir CPU, GPU y asignaciones antes de establecer limites.
- Comparar frame de UI idle y bajo interaccion.
- Verificar recursos al abrir y cerrar paneles.
- Evitar trabajo de paneles cerrados.

## 7. Validacion obligatoria

Cada incremento debe superar cuatro niveles:

1. Estatico: formato y `git diff --check`.
2. Compilacion: build completo del editor.
3. Funcional: tests existentes y nuevos tests de acciones, modelos y
   persistencia.
4. Visual: ejecucion real, capturas y revision contra la referencia.

La validacion funcional debe cubrir al menos:

- seleccion simple y multiple;
- orden, IDs y contenido de la seleccion;
- undo/redo y dirty state;
- drag and drop;
- scene tabs;
- prefab stage;
- Play Mode e input del viewport;
- asset import y estados de error;
- layout y paneles persistentes;
- autosave, cierre y restauracion.

Una compilacion correcta o un estado serializado no prueban que la interfaz se
vea o se comporte correctamente.

## 8. Estrategia de entrega

Los cambios deben ser incrementales y revisables:

1. Infraestructura sin cambios visibles.
2. Design tokens y galeria.
3. Shell del editor.
4. Un panel o seccion por incremento.
5. Validacion funcional y visual.
6. Retiro de la API anterior cuando no queden consumidores.

No se deben mezclar cambios de UI con renderer, gameplay o contenido del
proyecto externo. Cada commit debe contener un objetivo comprobable y preservar
los cambios locales no relacionados.

## 9. Estimacion global

La vertical principal deberia quedar disponible para evaluacion entre las
semanas 5 y 8. La migracion completa sobre ImGui se estima entre 10 y 16
semanas de trabajo sostenido.

Estas cifras son orientativas. Despues de la fase 0 se reemplazaran por
estimaciones basadas en mediciones del checkout y en el costo real de la
primera familia de componentes.

## 10. Definicion de terminado

El plan se considera completado cuando:

- Engine2D tiene una identidad visual coherente y propia.
- Workspaces y areas presentan una organizacion consistente.
- Los paneles centrales no exponen apariencia ImGui predeterminada.
- Acciones, atajos, menus y toolbars comparten un unico registro.
- La logica critica del editor puede probarse sin renderizar ImGui.
- Los recorridos funcionales no presentan regresiones.
- DPI, persistencia y empaquetado estan validados.
- El resultado visible fue aprobado mediante el editor ejecutado.
- La decision final de conservar ImGui o adoptar Qt/QML esta respaldada por la
  vertical y, si corresponde, por un spike integrado.
