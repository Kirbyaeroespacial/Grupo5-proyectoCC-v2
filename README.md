# 🛸 Proyecto Satélite-Estación Tierra

¡Bienvenido/a al proyecto! Esta guía te ayudará a configurar y ejecutar todo, incluso si es tu primera vez programando. 

## 🚀 Empezando: GitHub Codespaces

GitHub Codespaces es como tener VS Code en tu navegador. ¡Así no tienes que instalar nada en tu computadora!

### Paso 1: Abrir el Proyecto en Codespaces
1. Ve a la página principal de este repositorio en GitHub
2. Busca el botón verde que dice "Code" ▶️
3. Haz clic en la pestaña "Codespaces"
4. Clic en "Create codespace on main" 🆕

¡Espera un momento mientras se crea tu espacio de trabajo!

## 🌱 Trabajar con Git

Git es como un sistema súper avanzado para guardar tus cambios. Aquí está cómo usarlo:

### Para Trabajar en Nuevos Cambios
1. Siempre antes de empezar:

   ```bash
   git pull
   ```
   Esto descarga los últimos cambios que otros hayan hecho.

2. Cuando hagas cambios en archivos:
   ```bash
   git add .
   git commit -m "Explica aquí qué cambios hiciste"
   git push
   ```
   - `git add .` prepara todos tus cambios
   - `git commit` los guarda localmente con un mensaje
   - `git push` los sube a GitHub

### Si Te Equivocaste
- Para deshacer cambios en un archivo:
  ```bash
  git checkout nombre-del-archivo
  ```
- Para ver qué archivos has modificado:
  ```bash
  git status
  ```

## 🐍 Configurar y Ejecutar el Código Python

### Paso 1: Crear un Entorno Virtual
Un entorno virtual es como una burbuja especial para tu proyecto. Abre la terminal en VS Code (Ctrl+ñ o View -> Terminal) y escribe:

```bash
python -m venv .venv
```

### Paso 2: Activar el Entorno Virtual
En Windows:
```bash
.venv\Scripts\activate
```
En Mac/Linux:
```bash
source .venv/bin/activate
```

Sabrás que funcionó porque verás (.venv) al inicio de tu línea de comandos.

### Paso 3: Instalar lo Necesario
```bash
pip install -r requirements.txt
```
Este comando instala todos los paquetes que necesita el proyecto.

### Paso 4: Ejecutar el Programa
```bash
python src/ground_station/estacion_tierra.py
```

## 🔍 Estructura del Proyecto

```
proyecto_cc/
├── src/                    # Código fuente
│   ├── ground_station/     # Código Python (estación tierra)
│   └── arduino/           # Código Arduino (satélite)
├── tests/                 # Pruebas
├── docs/                  # Documentación
└── requirements.txt       # Lista de paquetes Python necesarios
```

## 📝 Consejos Importantes

1. **SIEMPRE** activa el entorno virtual antes de trabajar
2. Si instalas nuevos paquetes con pip, agrégalos a requirements.txt:
   ```bash
   pip freeze > requirements.txt
   ```
3. Si algo no funciona:
   - ¿Está activado el entorno virtual? (debe verse (.venv) en la terminal)
   - ¿Instalaste los requirements.txt?
   - ¿Hiciste git pull para tener la última versión?

## 🆘 ¿Necesitas Ayuda?

1. Revisa los mensajes de error con calma
2. Busca el error en Google (¡todos lo hacemos!)
3. Pregunta a tus compañeros o profesores
4. Revisa la documentación en la carpeta `docs/`

## 🎯 Para Empezar

1. Abre el proyecto en Codespaces (siguiendo los pasos de arriba)
2. Activa el entorno virtual
3. Instala los requirements.txt
4. ¡Empieza a programar!

¡Recuerda que todos fuimos principiantes alguna vez! No tengas miedo de preguntar o equivocarte, así es como se aprende. 😊
