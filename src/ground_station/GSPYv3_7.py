"""
sat_control.py  — GUI + serial handler
- Acepta mensajes con o sin checksum "*XX"
- Envía comandos con checksum
- Parsea paquetes (1..9,67,99)
"""

import time, threading, re
from collections import deque
from tkinter import *
from tkinter import messagebox, font
import serial
import matplotlib
matplotlib.use("TkAgg")
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg

# ---------------- CONFIG SERIAL (ajusta puerto) ----------------
DEVICE = 'COM7'   # <- cambia esto al puerto correcto
BAUD = 9600

try:
    usbSerial = serial.Serial(DEVICE, BAUD, timeout=1)
    print("✓ Puerto", DEVICE, "abierto")
except Exception as e:
    print("ERROR abriendo puerto:", e)
    raise SystemExit(1)

# ---------------- Datos compartidos ----------------
max_points = 100
temps = deque([0]*max_points, maxlen=max_points)
hums = deque([0]*max_points, maxlen=max_points)
temps_med = deque([0]*max_points, maxlen=max_points)
latest_data = {"temp": 0.0, "hum": 0.0}
latest_distance = 0
angulo = 90
latest_temp_med = 0.0
orbit_x = []
orbit_y = []
orbit_lock = threading.Lock()

# Stats
total_corrupted = 0
last_data_received = time.time()
connection_status = "Esperando..."
connection_lock = threading.Lock()

# Regex para posición orbital (lectura humana)
regex_orbit = re.compile(r"Position: \(X: ([\d\.-]+) m, Y: ([\d\.-]+) m, Z: ([\d\.-]+) m\)")

# ---------------- checksum (XOR - same as Arduino) ----------------
def calc_checksum(msg: str) -> str:
    xor_sum = 0
    for c in msg:
        xor_sum ^= ord(c)
    return format(xor_sum, '02X')

def send_command_with_checksum(cmd: str):
    chk = calc_checksum(cmd)
    full = f"{cmd}*{chk}\n"
    usbSerial.write(full.encode())
    print("TX>", full.strip())

def validate_message(line: str):
    """Acepta con '*' y sin '*'. Devuelve (valid, clean_msg)"""
    line = line.strip()
    if not line:
        return False, ""
    if '*' not in line:
        # legacy: accept but warn
        return True, line
    parts = line.split('*')
    if len(parts) != 2:
        return False, ""
    msg = parts[0]
    recv = parts[1].strip().upper()
    calc = calc_checksum(msg).upper()
    if recv == calc:
        return True, msg
    else:
        print(f"ERR CK expected={calc} recv={recv}")
        return False, ""

# ---------------- lector de serial en background ----------------
def read_serial_loop():
    global latest_distance, angulo, latest_temp_med, total_corrupted, last_data_received
    while True:
        try:
            raw = usbSerial.readline().decode('utf-8', errors='ignore').strip()
        except Exception as e:
            print("Serial read error:", e)
            time.sleep(0.1)
            continue
        if not raw:
            time.sleep(0.01)
            continue

        last_data_received = time.time()

        # Filtra mensajes de log del ground (si se imprimen)
        if raw.startswith("TX>") or raw.startswith("RX<") or raw.startswith("=== ") or raw.startswith("WARN") or raw.startswith("ERR"):
            print("[GND]", raw)
            continue

        # chequeo orbita formato humano
        m = regex_orbit.search(raw)
        if m:
            try:
                x = float(m.group(1)); y = float(m.group(2))
                with orbit_lock:
                    orbit_x.append(x); orbit_y.append(y)
                    if len(orbit_x) > 2000:
                        orbit_x.pop(0); orbit_y.pop(0)
                print("ORBIT: x,y =", x, y)
            except:
                pass
            continue

        valid, clean = validate_message(raw)
        if not valid:
            total_corrupted += 1
            print("CORRUPT MESSAGE - total:", total_corrupted)
            continue

        # ahora clean contiene "TYPE:payload" o una linea legacy
        parts = clean.split(':')
        if len(parts) >= 2:
            tid = parts[0]
            if tid == '1':
                try:
                    hum = int(parts[1]) / 100.0
                    temp = int(parts[2]) / 100.0
                    latest_data["temp"] = temp; latest_data["hum"] = hum
                    print(f"Temp: {temp:.2f}C  Hum: {hum:.2f}%")
                except:
                    pass
            elif tid == '2':
                try:
                    latest_distance = int(parts[1])
                    print("Dist:", latest_distance)
                except:
                    pass
            elif tid == '6':
                try:
                    angulo = int(parts[1])
                except:
                    pass
            elif tid == '7':
                try:
                    latest_temp_med = int(parts[1]) / 100.0
                    print("Temp media:", latest_temp_med)
                except:
                    pass
            elif tid == '8':
                print("ALERTA temperatura >100!")
                # Mostrar mensaje
                try:
                    messagebox.showwarning("Alerta", "Temperatura media >100°C")
                except:
                    pass
            elif tid == '9':
                # payload: time:x:y:z
                # Podríamos parsear; aquí imprimimos
                print("ORBIT DATA:", ":".join(parts[1:]))
            elif tid == '67':
                # token messages: usually 67:0 release, 67:1 (from ground)
                print("TOKEN MSG:", parts[1] if len(parts)>1 else "")
            elif tid == '99':
                print("STATS from ground:", ":".join(parts[1:]))
        else:
            # mensaje sin ':' (ej. 'g' heartbeat)
            if clean == 'g':
                print("HEARTBEAT")
        time.sleep(0.005)

# lanzar hilo lector
t = threading.Thread(target=read_serial_loop, daemon=True)
t.start()

# ---------------- GUI minimal y plots ----------------
root = Tk()
root.title("Control Satélite")
root.geometry("1200x700")
title_font = font.Font(size=16, weight="bold")

# Estado label
status_label = Label(root, text="Conectando...", font=title_font)
status_label.pack()

def update_status():
    dt = time.time() - last_data_received
    if dt < 6:
        s = "🟢 CONECTADO"
    elif dt < 12:
        s = "🟡 SEÑAL DÉBIL"
    else:
        s = "🔴 SIN SEÑAL"
    status_label.config(text=s)
    root.after(1000, update_status)

update_status()

# Controles simples
frame = Frame(root)
frame.pack(pady=10)

entry = Entry(frame, width=20)
entry.grid(row=0, column=0, padx=5)
entry.insert(0, "2000")  # ms default

def send_period():
    v = entry.get().strip()
    try:
        ms = int(v)
        if 200 <= ms <= 10000:
            send_command_with_checksum(f"1:{ms}")
            print("Sent set period", ms)
        else:
            messagebox.showerror("Error", "Periodo fuera de rango (200-10000)")
    except:
        messagebox.showerror("Error", "Valor no valido")

Button(frame, text="Set period (ms)", command=send_period).grid(row=0, column=1, padx=5)

def start_tx():
    send_command_with_checksum("3:i")
def stop_tx():
    send_command_with_checksum("3:p")
def resume_tx():
    send_command_with_checksum("3:r")

Button(frame, text="Start TX", command=start_tx).grid(row=0, column=2, padx=5)
Button(frame, text="Stop TX", command=stop_tx).grid(row=0, column=3, padx=5)
Button(frame, text="Resume TX", command=resume_tx).grid(row=0, column=4, padx=5)

# Graficas (temp/hum)
fig, ax = plt.subplots(1,1, figsize=(7,4))
lineT, = ax.plot(range(max_points), list(temps), label="T")
lineH, = ax.plot(range(max_points), list(hums), label="H")
ax.set_ylim(0, 100)
ax.legend()
canvas = FigureCanvasTkAgg(fig, master=root)
canvas.get_tk_widget().pack()

def update_plots():
    temps.append(latest_data["temp"])
    hums.append(latest_data["hum"])
    lineT.set_ydata(temps)
    lineH.set_ydata(hums)
    ax.relim(); ax.autoscale_view()
    canvas.draw()
    root.after(200, update_plots)

update_plots()
root.mainloop()
