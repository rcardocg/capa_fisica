RESTRICCIONES REALES DEL HARDWARE:

Plataforma objetivo: BeagleBone Black Rev C.
Sistema operativo: Debian GNU/Linux 9 Stretch.
Kernel: 4.14.108-ti-r119.
Python 2: 2.7.3 (no utilizar).
Python 3: 3.5.3 (runtime objetivo obligatorio).

Compatibilidad:
- Todo el código debe ejecutarse en Python 3.5.3.
- No utilizar f-strings.
- No utilizar dataclasses.
- No utilizar sintaxis o APIs exclusivas de versiones modernas de Python.
- Priorizar Standard Library.
- No asumir que las últimas versiones de paquetes son compatibles.
- Fijar versiones de todas las dependencias externas.

Hardware:
- Evaluar Adafruit_BBIO 1.1.0 como primera alternativa GPIO.
- Verificar compatibilidad antes de instalar.
- No asumir que todos los pines están disponibles.
- Considerar la configuración pin mux.
- Evaluar PRU-ICSS del AM3358 como alternativa para operaciones deterministas.
- Mantener separada la interfaz Python de la implementación GPIO y PRU.
- No implementar PRU hasta justificarlo mediante pruebas de temporización.

Despliegue:
- Debian Stretch puede no tener repositorios originales disponibles.
- La migración debe poder realizarse offline.
- No copiar el entorno virtual del escritorio.
- Crear el venv en la BeagleBone.
- Preparar wheels ARM compatibles con Python 3.5.
- Separar dependencias de desarrollo y hardware.

Antes de implementar:
- Verificar uname -m y dpkg --print-architecture.
- Verificar la versión de pip.
- Verificar la disponibilidad de GPIO y remoteproc.
- Documentar las incompatibilidades detectadas.

La prioridad es producir un backend que funcione en el hardware existente, no modernizar innecesariamente el sistema operativo.
# capa_fisica
