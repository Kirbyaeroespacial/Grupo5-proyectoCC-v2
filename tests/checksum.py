#!/usr/bin/env python3
# checksum_tester.py
# Lee líneas del puerto serie en formato: <payload>|<checksum>
# Calcula checksum y reporta PASS/FAIL. Contiene pruebas automáticas para ejemplos.

import serial
import time

PORT = 'COM3'      # Cambia a tu puerto ('/dev/ttyUSB0' en Linux)
BAUD = 9600
TIMEOUT = 2.0

def checksum_bytes(s: str) -> int:
    """Suma de bytes (UTF-8) % 256"""
    b = s.encode('utf-8', errors='replace')
    return sum(b) & 0xFF

def comprobar_linea(line: str) -> tuple:
    """Devuelve (payload, checksum_recibido:int, checksum_calc:int, correcto:bool)"""
    line = line.rstrip('\r\n')
    if '|' not in line:
        return (line, None, None, False)
    payload, chk_str = line.rsplit('|', 1)
    payload = payload.strip()
    chk_str = chk_str.strip()
    try:
        chk_rec = int(chk_str)
    except ValueError:
        return (payload, None, None, False)
    chk_calc = checksum_bytes(payload)
    return (payload, chk_rec, chk_calc, chk_rec == chk_calc)

def main():
    print("Opening serial port:", PORT)
    ser = serial.Serial(PORT, BAUD, timeout=TIMEOUT)
    time.sleep(1.0)  # dejar tiempo a que puerto se estabilice

    # Tests automáticos con las 3 cadenas del enunciado:
    ejemplos = [
        "Hola, mundo!",
        "Hola mundo!",
        "Hola. mundo!"
    ]
    print("\n=== Test local de checksums para ejemplos conocidos ===")
    for s in ejemplos:
        print(f"Payload: '{s}' -> checksum esperado (calc): {checksum_bytes(s)}")

    print("\n=== Escuchando puerto serie y validando mensajes entrantes ===")
    print("Recibiendo líneas en formato payload|CHK ... Ctrl-C para salir.\n")
    try:
        while True:
            linea_bytes = ser.readline()
            if not linea_bytes:
                # timeout sin datos
                continue
            try:
                linea = linea_bytes.decode('utf-8', errors='replace').strip()
            except Exception:
                linea = linea_bytes.decode('latin1', errors='replace').strip()
            payload, chk_rec, chk_calc, ok = comprobar_linea(linea)
            if chk_rec is None:
                print(f"[INVALID FORMAT] {linea}")
            else:
                status = "PASS" if ok else "FAIL"
                print(f"[{status}] payload='{payload}' recv={chk_rec} calc={chk_calc}")
    except KeyboardInterrupt:
        print("\nCerrando puerto y saliendo.")
    finally:
        ser.close()

if __name__ == "__main__":
    main()
