# GproyectoCC
Proyecto Satélite-Estación Tierra

## Project Structure

```
proyecto_cc/
├── src/                    # Source code
│   ├── ground_station/     # Python code for ground control
│   │   └── estacion_tierra.py
│   └── arduino/           # Arduino C/C++ code
├── tests/                 # Test files
├── docs/                  # Documentation
├── requirements.txt       # Python dependencies
└── .gitignore            # Git ignore rules
`

## Components

### Ground Station (Python)
Located in `src/ground_station/`, this component handles the ground control station functionality.

### Arduino (C/C++)
Located in `src/arduino/`, this component contains the Arduino code for the embedded systems.

## Setup and Installation

1. For Python components:
   ```bash
   pip install -r requirements.txt
   ```

2. For Arduino components:
   - Use Arduino IDE to compile and upload the code to your board

## Development

- Python code follows PEP 8 style guide
- Arduino code follows Arduino style guide
- Add appropriate tests in the `tests` directory
- Document new features in the `docs` directory
