# Arduino en Codespaces 🤖

Este documento explica cómo trabajar con Arduino en GitHub Codespaces.

## Configuración Inicial

El contenedor de desarrollo ya incluye:
- Arduino CLI
- Soporte para placas AVR (Arduino Uno, Nano, etc.)
- Extensiones de VS Code para Arduino

## Estructura de Archivos Arduino

```
src/arduino/
└── satellite/
    └── satellite.ino    # Código principal del satélite
```

## Trabajar con Arduino en Codespaces

### Para Compilar el Código
1. Abre el archivo `satellite.ino`
2. Haz clic en el botón "Verify" (✓) en la barra de estado
   O usa el comando: Ctrl+Alt+R

### Para Subir el Código (Cuando estés en local)
1. Conecta tu Arduino por USB
2. Selecciona el puerto correcto
3. Haz clic en el botón "Upload" (→) en la barra de estado
   O usa el comando: Ctrl+Alt+U

### Notas Importantes
- La carga de código solo funciona en local, no en Codespaces
- Puedes usar Codespaces para escribir y verificar el código
- Para cargar el código al Arduino, necesitas clonar el repo localmente

## Bibliotecas Utilizadas
- DHT sensor library (para el sensor DHT11)
- SoftwareSerial (incluida en Arduino)
