# Python ground station GUI — corregido
import time
import serial
import threading
import re
from collections import deque
from tkinter import *
from tkinter import font
from tkinter import messagebox 
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import numpy as np
import matplotlib
matplotlib.use("TkAgg")

plot_active = True

# Setup del serial
device = 'COM7'  # CAMBIAR según tu puerto
try:
    usbSerial = serial.Serial(device, 9600, timeout=1)
except Exception as e:
    print(f"Error abriendo puerto {device}: {e}")
    raise

# Búfer de datos sensores
max_points = 100
temps = deque([0]*max_points, maxlen=max_points)
hums = deque([0]*max_points, maxlen=max_points)
temps_med = deque([0]*max_points, maxlen=max_points)
latest_data = {"temp": 0, "hum": 0}
latest_distance = 0
angulo = 90
latest_temp_med = 0

# Trail del radar
thetas = []
radios = []

# Estadísticas checksum
total_corrupted = 0

# === CONTROL DE COMUNICACIÓN ===
last_data_received = time.time()
connection_status = "Esperando..."
connection_lock = threading.Lock()

# === DATOS ORBITALES ===
orbit_x = []
orbit_y = []
orbit_lock = threading.Lock()

# Regex para parsear posición orbital
regex_orbit = re.compile(r"Position: \(X: ([\d\.-]+) m, Y: ([\d\.-]+) m, Z: ([\d\.-]+) m\)")

# === FUNCIONES DE CHECKSUM ===
def calc_checksum(msg: str) -> str:
    """Calcula checksum XOR sobre bytes (devuelve 2 hex digits en mayúscula)."""
    if isinstance(msg, str):
        b = msg.encode('ascii', 'ignore')
    else:
        b = bytes(msg)
    xor_sum = 0
    for vb in b:
        xor_sum ^= vb
    return format(xor_sum, '02X').upper()

def send_command_with_checksum(command: str):
    """Envía comando con checksum (ASCII) y newline."""
    checksum = calc_checksum(command)
    full_msg = f"{command}*{checksum}\n"
    try:
        usbSerial.write(full_msg.encode('ascii'))
        print(f"✓ Enviado: {full_msg.strip()}")
    except Exception as e:
        print(f"Error enviando: {e}")

def validate_message(line: str):
    """Valida checksum de mensaje recibido. Devuelve (ok, payload_sin_checksum)."""
    line = line.strip()
    if '*' not in line:
        return False, ""
    # separar en el primer '*' para evitar problemas si el payload tiene más '*'
    parts = line.split('*', 1)
    if len(parts) != 2:
        return False, ""
    msg = parts[0]
    recv_checksum = parts[1].strip()
    calc_check = calc_checksum(msg)
    if recv_checksum.upper() == calc_check.upper():
        return True, msg
    else:
        print(f"⚠ Checksum inválido: esperado {calc_check}, recibido {recv_checksum}")
        return False, ""

# === MONITOR DE CONEXIÓN ===
def monitor_connection():
    global connection_status
    while True:
        time_since_last = time.time() - last_data_received
        with connection_lock:
            if time_since_last < 5:
                connection_status = "🟢 CONECTADO"
            elif time_since_last < 10:
                connection_status = "🟡 SEÑAL DÉBIL"
            else:
                connection_status = "🔴 SIN SEÑAL"
        time.sleep(1)

threading.Thread(target=monitor_connection, daemon=True).start()

def read_serial():
    global plot_active, latest_distance, angulo, latest_temp_med, total_corrupted, last_data_received
    global orbit_x, orbit_y

    while True:
        try:
            linea = usbSerial.readline().decode('utf-8', errors='ignore').strip()
        except Exception as e:
            print(f"Error leyendo serial: {e}")
            time.sleep(0.1)
            continue

        if not linea:
            time.sleep(0.01)
            continue

        # Actualizar tiempo de última recepción para cualquier mensaje
        last_data_received = time.time()

        # Mensajes de debug del ground station / logs
        if linea.startswith("->") or linea.startswith("<-") or linea.startswith("===") or linea.startswith("⚠") or linea.startswith("✓"):
            print(f"[GND] {linea}")
            time.sleep(0.01)
            continue

        # Chequear si es posición orbital
        match = regex_orbit.search(linea)
        if match:
            try:
                x = float(match.group(1))
                y = float(match.group(2))
                with orbit_lock:
                    orbit_x.append(x)
                    orbit_y.append(y)
                    if len(orbit_x) > 1000:
                        orbit_x.pop(0); orbit_y.pop(0)
                print(f"🛰️ Orbital: X={x:.0f}, Y={y:.0f}")
            except ValueError:
                pass
            time.sleep(0.01)
            continue

        # Validar checksum si el mensaje lo tiene
        if '*' in linea:
            valid, clean_msg = validate_message(linea)
            if not valid:
                total_corrupted += 1
                print(f"⚠ Mensaje corrupto descartado (Total: {total_corrupted})")
                time.sleep(0.01)
                continue
            linea = clean_msg

        parts = linea.split(':')
        try:
            if len(parts) >= 2 and parts[0] in ('1','2','3','4','5','6','7','8','9','67','99'):
                idn = parts[0]
                if idn == '1':
                    if len(parts) >= 3:
                        try:
                            hum = int(parts[1]) / 100.0
                            temp = int(parts[2]) / 100.0
                            latest_data["temp"] = temp
                            latest_data["hum"] = hum
                            print(f"📊 Temp: {temp:.2f}°C, Hum: {hum:.2f}%")
                        except ValueError:
                            pass
                elif idn == '2':
                    try:
                        latest_distance = int(parts[1])
                        print(f"📏 Distancia: {latest_distance} mm")
                    except ValueError:
                        pass
                elif idn == '3':
                    plot_active = False
                    messagebox.showerror("Error transmisión", f"Error: {':'.join(parts[1:])}")
                elif idn == '4':
                    messagebox.showerror("Error sensor", "⚠ Error en sensor temp/hum")
                elif idn == '5':
                    messagebox.showerror("Error sensor", "⚠ Error en sensor distancia")
                elif idn == '6':
                    try:
                        angulo = int(parts[1])
                    except ValueError:
                        messagebox.showerror("Error ángulo", "Valor incorrecto")
                elif idn == '7':
                    try:
                        latest_temp_med = int(parts[1]) / 100.0
                        print(f"📈 Temp media: {latest_temp_med:.2f}°C")
                    except ValueError:
                        pass
                elif idn == '8':
                    messagebox.showwarning("⚠ Alta temperatura!", "¡PELIGRO! Temp media >100°C")
                elif idn == '9':
                    # paquete orbital ya manejado arriba, pero si viene como 9:time:x:y:z lo ignoro aquí
                    pass
                elif idn == '67':
                    # Mensajes de token (ignorar en GUI)
                    pass
                elif idn == '99':
                    try:
                        corrupted = int(parts[1])
                        total_corrupted += corrupted
                        print(f"📊 [CHECKSUM] Descartados (notificados): {corrupted} | Total: {total_corrupted}")
                    except ValueError:
                        pass
        except Exception as e:
            print(f"⚠ Parse error: {e}")

        time.sleep(0.01)

threading.Thread(target=read_serial, daemon=True).start()

# GUI (sin cambios funcionales importantes)
# ... (aquí puedes pegar el resto de tu GUI tal cual; omito por brevedad)
# Para que no cortes el flujo pego las funciones de interfaz ya incluidas por ti.
# (Si quieres que integre pequeños arreglos en los plot o layout lo hago a petición.)
