# Diario del proyecto oXygen

## 2026-03-07 - Assistant con memoria larga y contexto musical

### Memoria de análisis
- El assistant deja de depender de una ventana corta de pocos segundos.
- Ahora puede basarse en hasta `256 s` recientes del audio de entrada.
- Para no disparar el uso de memoria del plugin, ese histórico largo se conserva en una versión reducida para análisis, no como audio crudo full-rate.

### Género
- El assistant ya puede trabajar con un perfil de género explícito:
  - `Universal`
  - `Pop`
  - `Hip Hop`
  - `Electronic`
  - `Rock`
  - `Acoustic`
  - `Orchestral`
- Cada género modifica el criterio de:
  - balance tonal,
  - control de graves,
  - anchura estéreo,
  - densidad/loudness objetivo,
  - comportamiento esperado del maximizer.

### Dirección artística
- Se añade un segundo contexto explícito de intención:
  - `Balanced`
  - `Transparent`
  - `Warm`
  - `Punchy`
  - `Wide`
  - `Aggressive`
- Esto cambia cómo de conservadora o decidida es la propuesta:
  - cuánto corrige,
  - cuánto abre,
  - cuánto comprime,
  - cuánto empuja loudness.

### Lectura honesta
- El plugin no “adivina” de forma fiable el género ni la intención artística solo por escuchar audio.
- Por eso esta fase se implementa con contexto elegido por el usuario desde la UI.
- Es mucho más defendible que fingir una comprensión artística inexistente.

## 2026-03-07 - Afinado del proceso de mastering

### Maximizer
- El detector del maximizer ya no mira solo muestras discretas.
- Ahora estima picos intermedios entre muestras consecutivas para acercarse mejor a una protección `true peak` práctica sin cambiar la latencia del plugin.
- Resultado esperado:
  - menos riesgo de que se escapen picos entre muestras,
  - limitación algo más segura para uso de master.

### Stereo Imager
- El widening ahora analiza el estado estéreo de cada banda antes de aplicar anchura:
  - correlación de la banda,
  - relación `mid/side`,
  - energía media.
- Si una banda ya viene muy abierta o comprometida en mono, el widening se modera automáticamente.
- Además se añade compensación energética al abrir para que el módulo no infle tanto el nivel RMS de la banda solo por ensanchar.

### Assistant
- El assistant empieza a usar un `trim` preventivo antes del maximizer cuando detecta que su propia propuesta añade energía por EQ o anchura.
- La intención es que la cadena llegue más limpia al limitador:
  - menos picos artificiales por la propia recomendación,
  - más control desde el maximizer,
  - menos necesidad de que el último módulo resuelva todo por fuerza.
- También se reajusta el `drive` sugerido del maximizer teniendo en cuenta ese `trim` previo.

### Lectura honesta
- Sigue sin haber un medidor LUFS formal ni `true peak` de norma.
- Pero la cadena ya se comporta de forma más parecida a una herramienta de mastering prudente:
  - assistant más autoconsciente,
  - imager menos temerario,
  - maximizer más seguro ante picos reales.

## 2026-03-07 - El assistant ya analiza bastante mejor antes de decidir

### Cambio principal
- El histórico de análisis deja de ser una ventana corta fija y pasa a cubrir varios segundos recientes de audio de entrada.
- El assistant ya no decide solo con un pico, un RMS y una FFT aislada.

### Qué analiza ahora
- Pico real y `true peak` aproximado por interpolación.
- RMS global y RMS con puerta energética tipo programa.
- Crest factor global.
- Correlación estéreo y proporción `mid/side`.
- Espectro promedio multi-frame sobre varias ventanas FFT, no una sola instantánea.
- Desviaciones locales por cada una de las 15 bandas reales del EQ gráfico.
- Dinámica y estéreo por 4 bandas aproximadas para alimentar compresión e imager con criterios más finos.

### Qué decide ahora con eso
- EQ:
  - ya puede escribir una propuesta sobre las 15 bandas del módulo real,
  - corrige exceso general de graves/agudos,
  - intenta cortar acumulaciones locales en zonas problemáticas,
  - evita boosts grandes y mantiene la curva dentro de un rango conservador.
- Multiband Compressor:
  - ajusta crossovers y compresión por banda según crest y acumulación detectada,
  - aprieta más donde hay exceso y dinámica que conviene sujetar.
- Stereo Imager:
  - usa información estéreo por banda,
  - graves más contenidos,
  - apertura alta solo cuando la mezcla realmente lo necesita y no está ya demasiado abierta.
- Maximizer:
  - usa RMS con puerta, crest y `true peak` aproximado para calcular un `drive` más sensato,
  - deja `Ceiling` seguro y adapta `Release` según la dinámica observada.

### Lectura honesta
- Sigue sin ser un modelo entrenado ni un sistema tipo Ozone Assistant.
- Pero ya no responde a una foto demasiado corta ni a 4 reglas rígidas.
- Ahora se comporta más como un assistant heurístico serio y bastante más defendible para mastering.

## 2026-03-07 - El assistant empieza a pensar de verdad

### Cambio principal
- `MASTER ASSIST` deja de ser un preset fijo disparado por botón.
- Ahora el procesador conserva un histórico reciente del audio de entrada y, al pulsar el botón, genera una propuesta a partir de análisis real.

### Qué analiza
- Pico reciente.
- RMS reciente.
- Crest factor.
- Correlación estéreo.
- Relación `mid/side`.
- Reparto espectral aproximado en graves, medios y agudos mediante FFT sobre el material reciente.

### Qué decide con eso
- Ajustes de EQ básicos según balance tonal.
- Intensidad de compresión multibanda según crest y reparto espectral.
- Anchura del imager según correlación y cantidad de side.
- Drive/release del maximizer según crest, RMS y headroom observados.

### Lectura honesta
- Sigue sin ser IA inferida por modelo.
- Sí es ya un assistant que responde a la mezcla que ha escuchado en vez de aplicar siempre la misma receta.

## 2026-03-07 - Dinámica stereo-linked y limitación con lookahead

### Multiband Compressor
- El compresor multibanda deja de depender del `juce::dsp::Compressor` multicanal por canal independiente.
- Ahora cada banda usa una detección `stereo-linked`:
  - detector compartido entre canales,
  - misma reducción aplicada a `L/R`,
  - menor riesgo de movimiento de imagen estéreo en mastering.
- Se mantiene la arquitectura de 4 bandas y los crossovers actuales, pero el comportamiento dinámico ya es mucho más apropiado para bus master.

### Maximizer
- El maximizer deja de depender del `juce::dsp::Limiter` estándar.
- Ahora implementa un limitador propio con:
  - `stereo link`,
  - `lookahead` fijo interno,
  - recuperación por `release`,
  - `Ceiling` aplicado como techo real de salida.
- El parámetro `Threshold` pasa a funcionar como `drive` de entrada al limitador:
  - cuanto más bajo, más empuja contra el techo.

### Qué mejora esto
- Menos inestabilidad lateral en mezcla estéreo desbalanceada.
- Menos sensación de compresión “por lado”.
- Limitación más cercana a una herramienta de mastering que a un bloque DSP genérico.

### Pendiente tras esta fase
- `True peak` real o aproximado con oversampling.
- Oversampling opcional en el maximizer.
- Rediseño del EQ hacia un formato más mastering-friendly.

## 2026-03-07 - Stereo Imager revisado y assistant afinado

### Stereo Imager
- Revisado el módulo y los sliders:
  - las automatizaciones y attachments sí estaban bien conectados,
  - no se detectó un fallo de bypass oculto ni un error de routing interno.
- El problema real era más de diseño:
  - el widening era demasiado sutil para el uso esperado en el master,
  - en mezcla real podía dar la sensación de “no hacer nada”.
- Ajuste aplicado:
  - por encima de `1.0`, la ley de anchura del `side` pasa a ser más agresiva.
  - sigue sin “inventar” estéreo de una fuente completamente mono, pero ahora el widening en material estéreo es bastante más evidente.

### Master Assist
- Se ajustó el bloque del imager dentro del assistant para que la propuesta automática sea algo más audible en bandas altas sin abrir demasiado los graves.

## 2026-03-07 - Primera base hacia un mastering assistant usable

### Cambio de dirección
- Se abandona el planteamiento de vender el botón automático como `AI` real.
- El flujo automático pasa a orientarse como `MASTER ASSIST`:
  - menos marketing,
  - más preset inteligente conservador,
  - más coherencia con el estado real del DSP.

### Fiabilidad y producto
- Se implementó guardado/restauración del estado del plugin:
  - parámetros de módulos,
  - orden actual de la cadena en el `AudioProcessorGraph`.
- También se añadió notificación de cambios del graph hacia la UI para que el rack pueda refrescarse al cambiar el orden.

### Medición
- Los medidores de entrada y salida ya no duplican el mismo valor:
  - ahora muestran `L/R` reales.
- El analizador deja de asumir `44100` Hz fijo:
  - usa la sample rate real del host.
- La señal enviada al analizador deja de ser solo el canal izquierdo:
  - ahora usa suma mono de la salida procesada.

### Assistant
- El botón visible del header pasa a `MASTER ASSIST`.
- La lógica automática se rehízo para ser más conservadora:
  - EQ suave de claridad, no curva exagerada.
  - compresión multibanda moderada.
  - imager contenido.
  - maximizer con `Ceiling` más seguro (`-1.0 dB`) para esta fase.

### Lectura estratégica
- Este cambio no convierte todavía el plugin en un competidor directo de Ozone.
- Sí lo mueve de "demo con estética AI" a "base de suite de mastering más honesta y defendible".

## 2026-03-07 - Branding visible y maximizer revisado

### Logo en el plugin
- El halo definido en SVG no estaba apareciendo en la GUI del plugin.
- Motivo probable: el render SVG de JUCE no estaba reproduciendo ese efecto de forma fiable.
- Solución aplicada:
  - el branding del header del plugin ahora se dibuja directamente en JUCE con glow real en icono y tipografía.
  - se mantiene el SVG del repo, pero el render visible del plugin ya no depende de ese filtro SVG.

### Maximizer
- Se eliminó la etapa fija de `tanh` previa al limitador:
  - esa etapa introducía distorsión incluso cuando se esperaba un limitador más transparente.
- Se corrigió la lógica de `Ceiling`:
  - antes dependía incorrectamente de `Threshold`.
  - ahora actúa como atenuación final después del limitador.
- Observación importante para mastering:
  - el `Limiter` de JUCE trabaja por canal, no con detector stereo-linked.
  - eso significa que no hay un fallo de routing, pero sí un posible movimiento de imagen en material muy desbalanceado entre L/R.
- Conclusión:
  - el módulo se comporta ahora más como un limitador/mastering que como un saturador encubierto.

### Imager
- Revisado el flujo de separación por bandas y recomposición.
- No se detectó un fallo equivalente al del EQ ni un error de routing entre canales.
- Observación:
  - con anchos > `1.0` el módulo puede elevar picos, algo normal en procesamiento mid/side.
  - eso no implica por sí mismo una distorsión interna del módulo, pero sí puede empujar más al limitador o a la salida.

## 2026-03-07 - EQ multicanal corregido

### Problema detectado
- El módulo de EQ estaba usando `juce::dsp::IIR::Filter<float>` directamente sobre un bloque estéreo.
- Ese filtro de JUCE solo procesa bloques mono, así que el comportamiento observado de "solo afecta a un canal" tenía sentido.

### Corrección aplicada
- El EQ ahora usa `juce::dsp::ProcessorDuplicator` para duplicar cada banda por canal.
- Con esto, cada banda del ecualizador procesa correctamente toda la señal stereo del bus master o de una pista stereo.

### Revisión del resto de módulos
- `MultibandCompressorModule`: no presenta este mismo fallo; usa `processSample(channel, ...)` en crossovers y `juce::dsp::Compressor` multicanal.
- `StereoImagerModule`: no presenta este fallo; separa y recompone por canal explícitamente.
- `GainModule`: aplica ganancia al buffer completo.
- `MaximizerModule`: procesa todos los canales del buffer.
- `AudioProcessorGraph`: sigue conectando ambos canales en cadena hasta el output final.

### Verificación
- Recompilado correcto del plugin tras el cambio.
- Artefacto actualizado:
  - `releases/oXygen.vst3`

## 2026-03-07 - Corrección DSP y build verificado

### Qué se corrigió
- Enrutamiento del `AudioProcessorGraph`:
  - las conexiones ahora se crean en función de los canales realmente activos.
  - se evita cablear ciegamente dos canales cuando el layout es mono.
  - se validan conexiones antes de insertarlas en el graph.
- `MasteringModule`:
  - el soporte de layout exige ahora que entrada y salida coincidan en mono o stereo.
- Equalizer:
  - los filtros se reconfiguran correctamente al preparar el plugin y cuando cambia la sample rate.
  - se eliminó el riesgo de arrancar con coeficientes no actualizados por depender solo del cambio de ganancia.
  - las frecuencias se acotan a un rango seguro respecto a Nyquist.
- Multiband Compressor:
  - se inicializó correctamente la caché de parámetros internos.
  - se eliminó un parámetro erróneo heredado (`HighWidth`) que no pertenecía al compresor.
  - los crossovers se sanitizan para mantener orden y separación mínima.
  - se resetean filtros/compresores en `prepareToPlay`.
  - se sanea la salida si aparece algún valor no finito.
- Stereo Imager:
  - se resetean crossovers al preparar.
  - se sanitizan los crossovers y el ancho estéreo.
  - se corrige el resize de buffers temporales cuando cambia el número de canales.
  - se sanea la salida ante valores no finitos.
- Maximizer:
  - el parámetro `Ceiling` ya afecta al resultado como atenuación final después del limitador.
  - el limitador se resetea correctamente en `prepareToPlay`.
  - se sanea la salida ante valores no finitos.
- Build/CMake:
  - se retiró `-ffast-math` de las flags globales para no alterar la semántica numérica de JUCE ni generar warnings masivos de `infinity`.

### Verificación
- Configuración CMake completada correctamente descargando JUCE con `FetchContent`.
- Compilación completada correctamente en macOS.
- Artefacto generado:
  - `releases/oXygen.vst3`

### Observaciones tras compilar
- El build ya no arrastra la avalancha de warnings de `infinity` que provocaba `-ffast-math`.
- Siguen apareciendo advertencias de API de fuentes de JUCE y varias advertencias menores de estilo/conversiones.
- La compilación genera código cliente de formatos extra de JUCE durante el target VST3, aunque el artefacto final sigue siendo VST3.

### Pendiente después de esta corrección
- Persistencia de estado del plugin.
- Exposición en GUI de todos los parámetros relevantes del compresor/imager.
- Revisar si conviene retirar `-ffast-math` o acotarlo para evitar warnings y posibles efectos no deseados en DSP/JUCE.
- Validar auditivamente la respuesta de los módulos tras estos cambios.

## 2026-03-07 - Checkpoint inicial

### Resumen
- Proyecto de plugin de mastering en C++ con JUCE.
- Arquitectura modular basada en `juce::AudioProcessorGraph`.
- GUI propia con analizador, medidores y rack de módulos.
- Estado general: base funcional planteada, pero todavía en fase temprana/alfa.

### Qué hay implementado ahora mismo
- Procesador principal con cadena modular en este orden:
  - Equalizer
  - Multiband Compressor
  - Stereo Imager
  - Gain
  - Maximizer
- Reordenación de módulos dentro del graph.
- Interfaz principal con:
  - branding SVG
  - botón `AI MASTER`
  - spectrum analyzer
  - medidores de entrada/salida
  - rack de módulos con bypass, colapsado y mover arriba/abajo
- Módulos DSP presentes en código:
  - ecualizador gráfico
  - compresor multibanda
  - stereo imager multibanda
  - gain
  - maximizer
- Motor `InferenceEngine` preparado como esqueleto para IA futura.

### Estado real frente a lo que promete el README
- El README habla de `AI-Assisted Mastering`, pero ahora mismo el botón `AI MASTER` aplica una heurística fija, no una inferencia real con modelo.
- El README habla de un EQ de 16 bandas, pero el código implementa 15 bandas.
- El README indica soporte macOS `VST3, AU`, pero en CMake solo está activado `VST3`.
- El README vende funciones de mastering más maduras de lo que refleja el estado actual del código.

### Verificación realizada hoy
- Leído el `README.md` y el código principal del proyecto.
- Revisados:
  - build/CMake
  - processor/editor principal
  - GUI principal y rack de módulos
  - módulos DSP
  - esqueleto de IA
- Intentada una configuración con CMake.
- Resultado: el proyecto está preparado para descargar JUCE automáticamente mediante `FetchContent` durante la configuración si no existe la carpeta local `JUCE`.
- `build.sh` dispara precisamente ese flujo al ejecutar `cmake -B build ...` antes de compilar.
- En este entorno la configuración falló por falta de acceso de red hacia GitHub, así que el build no queda verificado desde este checkpoint, pero no porque el repositorio espere necesariamente una carpeta `JUCE/` ya presente.

### Riesgos y pendientes claros
- Persistencia no implementada:
  - `getStateInformation` y `setStateInformation` están vacíos.
  - Riesgo: el plugin no guardará/restaurará parámetros ni estado en el host.
- Dependencia externa de JUCE sin vendorizar:
  - si no existe `JUCE/`, el proyecto depende de `FetchContent` contra GitHub durante la configuración/build.
  - además se usa `GIT_TAG master`, poco estable para builds reproducibles.
- La parte de IA es placeholder:
  - `InferenceEngine` no está integrado en el flujo real de análisis.
  - no hay extracción de features ni modelo ONNX funcionando.
- El módulo de Gain está en la cadena, pero no tiene editor propio:
  - existe en DSP, pero no parece accesible desde el rack visual.
- El Maximizer ya aplica `Ceiling`, pero sigue pendiente decidir si conviene implementar detección stereo-linked para uso más fino en mastering.
- No todos los parámetros DSP están expuestos en la UI:
  - el compresor multibanda define ratio, attack y release por banda, pero el editor solo muestra threshold y gain.
  - el stereo imager tiene crossovers en DSP, pero el editor no los muestra.
- Los medidores y el analizador son simplificados:
  - nivel basado en magnitud/peak, no LUFS ni RMS real.
  - el analizador toma señal simplificada en vez de un pipeline de medición más completo.
  - el analizador usa `44100` Hz fijo en el dibujo, no la sample rate real del host.
  - los medidores L/R de entrada y salida reflejan el mismo valor en ambos lados.
- Hay indicios de código todavía en ajuste:
  - comentarios de "for now", "mock", "placeholder".
  - varias decisiones de GUI y DSP parecen provisionales.

### Problemas técnicos a revisar pronto
- Inicialización segura de cachés/parámetros internos en DSP.
- Validar la lógica de división y suma en multibanda para evitar artefactos.
- Confirmar que mono/stereo se comporta bien en toda la cadena.
- Revisar si la medición L/R realmente refleja ambos canales o solo una lectura simplificada.
- Conectar sample rate real al spectrum analyzer.
- Añadir guardado de estado por módulo y del orden del rack.
- Definir si el objetivo inmediato es:
  - convertir el proyecto en un plugin estable aunque sea sin IA real
  - o priorizar una primera integración real del motor de inferencia

### Próximos pasos recomendados
1. Hacer reproducible el build:
   - vendorizar JUCE o fijar una versión concreta.
2. Implementar persistencia de estado del plugin.
3. Alinear README con el estado real o cerrar las brechas del código.
4. Exponer y terminar los controles que hoy están incompletos, empezando por Gain y Maximizer.
5. Auditar DSP multibanda y medición antes de hablar de versión usable.
6. Decidir el alcance real de `AI MASTER` a corto plazo.

### Criterio para futuras entradas del diario
- Qué se cambió.
- Qué quedó funcionando.
- Qué no se pudo verificar.
- Qué problemas nuevos aparecieron.
- Qué queda pendiente para el siguiente checkpoint.
