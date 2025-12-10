#!/usr/bin/env python3
"""
Ground station GUI (Python) - sincronizado con los sketches Arduino.
No incluye cifrado. Envía comandos con checksum (XOR -> 2 hex digits).
Provee control manual (4:m / 4:a) y envío de ángulo manual (5:ANG).
"""

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

# extras
import datetime
import os
import sys
import traceback

# ---------- CONFIG ----------
DEVICE = 'COM7'     # Cambia aquí si tu puerto es distinto (ej. '/dev/ttyUSB0')
BAUDRATE = 9600
MAX_POINTS = 100
EVENTS_FILE = "eventos.txt"
# ---------------------------

# estado compartido
plot_active = True
latest_data = {"temp": 0.0, "hum": 0.0}
latest_distance = 0
angulo = 90
latest_temp_med = 0.0

temps = deque([0]*MAX_POINTS, maxlen=MAX_POINTS)
hums = deque([0]*MAX_POINTS, maxlen=MAX_POINTS)
temps_med = deque([0]*MAX_POINTS, maxlen=MAX_POINTS)

thetas = []
radios = []

total_corrupted = 0
corrupted_from_sat_reported = 0

orbit_x = []
orbit_y = []
orbit_lock = threading.Lock()

# regex para la línea de posición imprimida por la estación
regex_orbit = re.compile(r"Position: \(X: ([\d\.-]+) m, Y: ([\d\.-]+) m, Z: ([\d\.-]+) m\)")

# Asegurar fichero eventos
if not os.path.exists(EVENTS_FILE):
    open(EVENTS_FILE, "w", encoding="utf-8").close()

def registrar_evento(tipo, detalles=""):
    ahora = datetime.datetime.now()
    fecha_hora = ahora.strftime("%Y-%m-%d %H:%M:%S")
    linea = f"{fecha_hora}|{tipo}|{detalles}\n"
    try:
        with open(EVENTS_FILE, "a", encoding="utf-8") as f:
            f.write(linea)
    except Exception as e:
        print("Error registrando evento:", e)

def cargar_eventos():
    evs = []
    if not os.path.exists(EVENTS_FILE):
        return evs
    try:
        with open(EVENTS_FILE, "r", encoding="utf-8") as f:
            for ln in f:
                ln = ln.strip()
                if not ln:
                    continue
                parts = ln.split("|", 2)
                if len(parts) != 3:
                    continue
                try:
                    dt = datetime.datetime.strptime(parts[0], "%Y-%m-%d %H:%M:%S")
                except Exception:
                    continue
                evs.append((dt, parts[1], parts[2]))
    except Exception as e:
        print("Error leyendo eventos:", e)
    return evs

# ---------------- checksum ----------------
def calc_checksum(msg: str) -> str:
    """XOR checksum -> 2 uppercase hex digits"""
    xor = 0
    for ch in msg:
        xor ^= ord(ch)
    return format(xor, '02X').upper()

def build_message_with_checksum(msg: str) -> str:
    chk = calc_checksum(msg)
    return f"{msg}*{chk}\n"

# ---------- serial helper ----------
def open_serial(port, baud):
    try:
        ser = serial.Serial(port, baud, timeout=1)
        print(f"[SERIAL] abierto {port} @ {baud}")
        return ser
    except Exception as e:
        print(f"[SERIAL] no se pudo abrir {port}: {e}")
        return None

usbSerial = open_serial(DEVICE, BAUDRATE)

# validación entrante:
def validate_incoming(line: str):
    """
    Devuelve (ok:bool, clean_msg:str, recv_chk:str or None, calc_chk:str or None)
    Si no hay '*' se considera ok y clean_msg == line (compatibilidad legacy).
    """
    if '*' not in line:
        return True, line, None, None
    a = line.rfind('*')
    msg = line[:a]
    recv = line[a+1:].strip()
    calc = calc_checksum(msg)
    ok = (recv.upper() == calc.upper())
    return ok, msg, recv.upper(), calc.upper()

# ---------- lectura en hilo ----------
_stop_event = threading.Event()

def read_serial_loop():
    global latest_data, latest_distance, angulo, latest_temp_med
    global total_corrupted, corrupted_from_sat_reported, orbit_x, orbit_y

    reconnect_wait = 2.0
    while not _stop_event.is_set():
        try:
            if usbSerial is None or not getattr(usbSerial, "is_open", False):
                ser = open_serial(DEVICE, BAUDRATE)
                if ser:
                    globals()['usbSerial'] = ser
                else:
                    time.sleep(reconnect_wait)
                    continue

            bline = usbSerial.readline()
            if not bline:
                time.sleep(0.01)
                continue

            try:
                line = bline.decode('utf-8', errors='ignore').strip()
            except Exception:
                line = bline.decode('latin1', errors='ignore').strip()

            if not line:
                time.sleep(0.01)
                continue

            # detectar linea orbital (la estación imprime Position: (...) por Serial para python)
            m = regex_orbit.search(line)
            if m:
                try:
                    x = float(m.group(1)); y = float(m.group(2))
                    with orbit_lock:
                        orbit_x.append(x); orbit_y.append(y)
                    print(f"[ORB] X={x}, Y={y}")
                except ValueError:
                    pass
                time.sleep(0.01)
                continue

            ok, clean_msg, recv_chk, calc_chk = validate_incoming(line)
            if not ok:
                total_corrupted += 1
                print(f"[CHK ERR] mensaje corrupto recibido: {line} (recv={recv_chk} calc={calc_chk})")
                registrar_evento("alarma", f"Mensaje corrupto recibido: {line}")
                time.sleep(0.01)
                continue

            # procesar mensaje "limpio" (sin *CHK si lo traía)
            parts = clean_msg.split(':')
            try:
                if len(parts) >= 2 and parts[0] in ('1','2','3','4','5','6','7','8','67','99'):
                    idn = parts[0]
                    if idn == '1':
                        # formato esperado: 1:hum100:temp100
                        if len(parts) >= 3:
                            try:
                                hum = int(parts[1]) / 100.0
                                temp = int(parts[2]) / 100.0
                                latest_data["temp"] = temp
                                latest_data["hum"] = hum
                                print(f"[SENSOR] Temp {temp:.2f}°C Hum {hum:.2f}%")
                            except ValueError:
                                pass
                    elif idn == '2':
                        try:
                            latest_distance = int(parts[1])
                            print(f"[SENSOR] Distancia: {latest_distance} mm")
                        except ValueError:
                            pass
                    elif idn == '3':
                        # detener gráficos / error transmisión
                        plot_active = False
                        messagebox.showerror("Error transmisión", f"Error: {':'.join(parts[1:])}")
                        registrar_evento("alarma", "Error transmisión: " + ":".join(parts[1:]))
                    elif idn == '4':
                        messagebox.showerror("Error sensor", "Error en sensor temp/hum")
                        registrar_evento("alarma", "Error sensor temp/hum")
                    elif idn == '5':
                        messagebox.showerror("Error sensor", "Error en sensor distancia")
                        registrar_evento("alarma", "Error sensor distancia")
                    elif idn == '6':
                        try:
                            angulo = int(parts[1])
                        except ValueError:
                            messagebox.showerror("Error ángulo", "Valor incorrecto")
                    elif idn == '7':
                        try:
                            latest_temp_med = int(parts[1]) / 100.0
                        except ValueError:
                            pass
                    elif idn == '8':
                        messagebox.showinfo("Alta temperatura!", "¡PELIGRO! Temp media >100°C")
                        registrar_evento("alarma", "Temperatura media >100°C")
                    elif idn == '67':
                        # token (67:0 o 67:1) - no hacemos nada
                        pass
                    elif idn == '99':
                        try:
                            corrupted = int(parts[1])
                            corrupted_from_sat_reported += corrupted
                            total_corrupted += corrupted
                            print(f"[CHECKSUM] Descartes reportados por estación: {corrupted} | Total: {total_corrupted}")
                            registrar_evento("alarma", f"Mensajes corruptos reportados: {corrupted}")
                        except ValueError:
                            pass
            except Exception as e:
                print("Parse error:", e)
                traceback.print_exc()

        except Exception as e:
            print("[SERIAL ERROR] ", e)
            try:
                if usbSerial:
                    usbSerial.close()
            except:
                pass
            globals()['usbSerial'] = None
            time.sleep(1.0)

# lanzar hilo lector
reader_thread = threading.Thread(target=read_serial_loop, daemon=True)
reader_thread.start()

# ----------------- GUI -----------------
# Ventana principal
root = Tk()
root.title("Control Satélite")
root.geometry("1800x800")
root.configure(bg="#1e1e2f")
root.resizable(False, False)

title_font = font.Font(family="Inter", size=22, weight="bold")
button_font = font.Font(family="Inter", size=14, weight="bold")
col_izq = "#1e292f"
col_der = "#31434d"

# frame título + botones orbit / eventos
title_frame = Frame(root, bg="#1e1e2f")
title_frame.pack(pady=(20,10))
Label(title_frame, text="Control Satélite", font=title_font, bg="#1e1e2f", fg="white").pack(side=LEFT, padx=20)

# Ventana orbital (ventana independiente)
class VentanaOrbital:
    def __init__(self, parent):
        self.window = Toplevel(parent)
        self.window.title("Órbita Satelital")
        self.window.geometry("800x800")
        self.window.configure(bg="#1e1e2f")
        Label(self.window, text="Vista Orbital (Plano Ecuatorial)", font=("Inter",16,"bold"),
              bg="#1e1e2f", fg="white").pack(pady=10)
        self.fig, self.ax = plt.subplots(figsize=(7,7))
        self.orbit_line, = self.ax.plot([], [], 'bo-', markersize=2, label='Órbita')
        self.last_point = self.ax.scatter([], [], color='red', s=50, label='Posición actual')
        R_EARTH = 6371000
        earth = plt.Circle((0,0), R_EARTH, color='orange', fill=False, linewidth=2, label='Tierra')
        self.ax.add_artist(earth)
        self.ax.set_xlim(-7e6, 7e6)
        self.ax.set_ylim(-7e6, 7e6)
        self.ax.set_aspect('equal', 'box')
        self.ax.set_xlabel('X (metros)'); self.ax.set_ylabel('Y (metros)')
        self.ax.grid(True); self.ax.legend()
        self.canvas = FigureCanvasTkAgg(self.fig, master=self.window)
        self.canvas.get_tk_widget().pack(expand=True, fill=BOTH)
        self.active = True
        self.window.protocol("WM_DELETE_WINDOW", self.on_close)
        self.update_plot()

    def update_plot(self):
        if not self.active:
            return
        with orbit_lock:
            if len(orbit_x) > 0:
                self.orbit_line.set_data(orbit_x, orbit_y)
                self.last_point.set_offsets([[orbit_x[-1], orbit_y[-1]]])
                try:
                    max_coord = max(max(abs(x) for x in orbit_x), max(abs(y) for y in orbit_y))
                    if max_coord > 6.5e6:
                        lim = max_coord * 1.1
                        self.ax.set_xlim(-lim, lim); self.ax.set_ylim(-lim, lim)
                except ValueError:
                    pass
        self.canvas.draw()
        self.window.after(500, self.update_plot)

    def on_close(self):
        self.active = False
        self.window.destroy()

orbital_window = None
def open_orbital():
    global orbital_window
    if orbital_window is None or not orbital_window.active:
        orbital_window = VentanaOrbital(root)

Button(title_frame, text="🛰️ Ver Órbita", font=("Inter",12,"bold"), command=open_orbital,
       bg="#6b8dd6", fg="white", bd=0, padx=15, pady=8).pack(side=LEFT)

# Botón Ver eventos
def abrir_vista_eventos():
    ev_win = Toplevel(root)
    ev_win.title("Eventos")
    ev_win.geometry("900x600")
    ev_win.configure(bg="#1e1e2f")
    filtro_frame = Frame(ev_win, bg="#1e1e2f")
    filtro_frame.pack(pady=8, fill=X)
    Label(filtro_frame, text="Tipo:", bg="#1e1e2f", fg="white").pack(side=LEFT, padx=6)
    tipo_var = StringVar(value="todos")
    tipo_menu = OptionMenu(filtro_frame, tipo_var, "todos", "comando", "alarma", "observacion")
    tipo_menu.pack(side=LEFT, padx=6)
    Label(filtro_frame, text="Desde (dd-mm-YYYY HH:MM:SS):", bg="#1e1e2f", fg="white").pack(side=LEFT, padx=6)
    desde_entry = Entry(filtro_frame, width=20); desde_entry.pack(side=LEFT, padx=6)
    Label(filtro_frame, text="Hasta (dd-mm-YYYY HH:MM:SS):", bg="#1e1e2f", fg="white").pack(side=LEFT, padx=6)
    hasta_entry = Entry(filtro_frame, width=20); hasta_entry.pack(side=LEFT, padx=6)
    text_box = Text(ev_win, wrap=WORD, bg="#0f1720", fg="white"); text_box.pack(expand=True, fill=BOTH, padx=8, pady=8)
    def aplicar_filtro():
        tipo = tipo_var.get(); desde = desde_entry.get().strip(); hasta = hasta_entry.get().strip()
        start_dt = None; end_dt = None
        try:
            if desde:
                start_dt = datetime.datetime.strptime(desde, "%d-%m-%Y %H:%M:%S")
            if hasta:
                end_dt = datetime.datetime.strptime(hasta, "%d-%m-%Y %H:%M:%S")
        except Exception:
            messagebox.showerror("Formato fecha", "Usa formato dd-mm-YYYY HH:MM:SS")
            return
        tipo_filter = tipo if tipo != "todos" else None
        eventos = filtrar_eventos(tipo_filter=tipo_filter, start_dt=start_dt, end_dt=end_dt)
        text_box.delete("1.0", END)
        for dt, tp, desc in eventos:
            text_box.insert(END, f"{dt.strftime('%d-%m-%Y %H:%M:%S')}  [{tp}]  {desc}\n")
    btnf = Button(filtro_frame, text="Aplicar filtro", command=aplicar_filtro, bg="#4b6cb7", fg="white"); btnf.pack(side=LEFT, padx=6)

Button(title_frame, text="Ver eventos", font=("Inter",12,"bold"), command=abrir_vista_eventos,
       bg="#6b8dd6", fg="white", bd=0, padx=15, pady=8).pack(side=LEFT)

# Entry para velocidad (1:vel)
entry = Entry(root, font=("Inter",14), fg="#1e1e2f")
entry.pack(pady=20, ipadx=80, ipady=5)
placeholder = "Tiempo entre datos o evento"
entry.insert(0, placeholder)
def on_entry_click(event):
    if entry.get() == placeholder:
        entry.delete(0, END); entry.config(fg="black")
def on_focus_out(event):
    if entry.get() == "":
        entry.insert(0, placeholder); entry.config(fg="gray")
entry.bind("<FocusIn>", on_entry_click); entry.bind("<FocusOut>", on_focus_out)

def leer_vel():
    vel_raw = entry.get()
    if vel_raw == placeholder or vel_raw == "":
        messagebox.showerror("Error", "Introduzca valor entre 200-10000 ms"); return
    try:
        vel = int(vel_raw)
        if 200 <= vel <= 10000:
            full = build_message_with_checksum(f"1:{vel}")
            try:
                if usbSerial and usbSerial.is_open:
                    usbSerial.write(full.encode())
                else:
                    messagebox.showwarning("Serial", "Puerto serial no abierto")
            except Exception as e:
                print("Error enviando serial:", e)
            registrar_evento("comando", f"1:{vel}")
            messagebox.showinfo("OK", f"Velocidad: {vel} ms")
        else:
            messagebox.showerror("Error", f"Fuera de rango: {vel}")
    except ValueError:
        messagebox.showerror("Error", f"No numérico: {vel_raw}")

btn_validar = Button(root, text="Validar", font=("Inter",14,"bold"), command=leer_vel, bg="#4b6cb7", fg="white", bd=0, padx=20, pady=10)
btn_validar.pack(pady=10)

# Layout frames
left_frame = Frame(root, bg=col_izq, width=900, height=600); left_frame.pack(side=LEFT, fill=BOTH)
right_frame = Frame(root, bg=col_der, width=900, height=600); right_frame.pack(side=RIGHT, fill=BOTH, expand=True)
left_frame.pack_propagate(0); right_frame.pack_propagate(0)

# Left buttons (start/stop/reanudar)
btn_frame_left = Frame(left_frame, bg=col_izq); btn_frame_left.pack(pady=10)
def create_btn(master, text, command): return Button(master, text=text, command=command, font=button_font, bg="#4b6cb7", fg="white", bd=0, padx=20, pady=15, width=18)
def send_cmd_str(cmd):
    full = build_message_with_checksum(cmd)
    try:
        if usbSerial and usbSerial.is_open:
            usbSerial.write(full.encode())
        else:
            print("[WARN] puerto serial no abierto")
    except Exception as e:
        print("Error enviando serial:", e)
    registrar_evento("comando", cmd)
def iniClick():
    global plot_active; send_cmd_str("3:i"); plot_active = True
def stopClick():
    global plot_active; send_cmd_str("3:p"); plot_active = False
def reanClick():
    global plot_active; send_cmd_str("3:r"); plot_active = True

create_btn(btn_frame_left, "Iniciar transmisión", iniClick).grid(row=0, column=0, padx=10)
create_btn(btn_frame_left, "Parar transmisión", stopClick).grid(row=0, column=1, padx=10)
create_btn(btn_frame_left, "Reanudar", reanClick).grid(row=0, column=2, padx=10)

# Observaciones
obs_frame = Frame(left_frame, bg=col_izq); obs_frame.pack(pady=8)
Label(obs_frame, text="Observación:", bg=col_izq, fg="white", font=("Inter",11)).grid(row=0, column=0, padx=6)
obs_entry = Entry(obs_frame, width=50, font=("Inter",11)); obs_entry.grid(row=0, column=1, padx=6)
def agregar_observacion():
    txt = obs_entry.get().strip()
    if not txt:
        messagebox.showwarning("Observación vacía", "Escribe una observación antes de guardar."); return
    registrar_evento("observacion", txt); messagebox.showinfo("OK", "Observación registrada"); obs_entry.delete(0, END)
Button(obs_frame, text="Añadir observación", command=agregar_observacion, font=("Inter",11), bg="#6b8dd6", fg="white").grid(row=0, column=2, padx=6)

# Plot temp/hum
fig_plot, ax_plot = plt.subplots(figsize=(7,4.5))
ax_plot.set_ylim(0,100)
ax_plot.set_title("Temperatura y Humedad")
line_temp, = ax_plot.plot(range(MAX_POINTS), temps, label="Temperature")
line_hum, = ax_plot.plot(range(MAX_POINTS), hums, label="Humidity")
line_med, = ax_plot.plot(range(MAX_POINTS), temps_med, label="Avg. temp")
ax_plot.legend()
canvas_plot = FigureCanvasTkAgg(fig_plot, master=left_frame)
canvas_plot.get_tk_widget().pack(pady=20)

def update_plot():
    temps.append(latest_data["temp"]); hums.append(latest_data["hum"]); temps_med.append(latest_temp_med)
    line_temp.set_visible(plot_active); line_hum.set_visible(plot_active); line_med.set_visible(plot_active)
    line_temp.set_ydata(temps); line_hum.set_ydata(hums); line_med.set_ydata(temps_med)
    ax_plot.relim(); ax_plot.autoscale_view()
    canvas_plot.draw()
    root.after(100, update_plot)

# Right controls (OS Auto / Manual + manual angle send)
btn_frame_right = Frame(right_frame, bg=col_der); btn_frame_right.pack(pady=10)
def os_auto():
    send_cmd_str("4:a")
def os_man():
    send_cmd_str("4:m")
create_btn(btn_frame_right, "OS Auto", os_auto).grid(row=0, column=0, padx=10)
create_btn(btn_frame_right, "OS Manual", os_man).grid(row=0, column=1, padx=10)

# Manual angle input + send
man_frame = Frame(right_frame, bg=col_der); man_frame.pack(pady=12)
Label(man_frame, text="Ángulo manual (0-180):", bg=col_der, fg="white").grid(row=0, column=0, padx=6)
angle_entry = Entry(man_frame, width=6, font=("Inter",12)); angle_entry.grid(row=0, column=1, padx=6)
def send_manual_angle():
    v = angle_entry.get().strip()
    try:
        ang = int(v)
        if 0 <= ang <= 180:
            send_cmd_str(f"5:{ang}")
            messagebox.showinfo("Enviado", f"Ángulo {ang} enviado (5:{ang})")
        else:
            messagebox.showerror("Error", "Ángulo fuera de rango (0-180)")
    except ValueError:
        messagebox.showerror("Error", "Ángulo no numérico")
Button(man_frame, text="Enviar Ángulo", command=send_manual_angle, bg="#4b6dd6", fg="white").grid(row=0, column=2, padx=6)

# Radar plot (polar)
fig_rad, ax_rad = plt.subplots(subplot_kw={'polar': True}, figsize=(7,4.5))
max_distance = 500
ax_rad.set_ylim(0, max_distance)
ax_rad.set_thetamin(0); ax_rad.set_thetamax(180)
ax_rad.set_theta_zero_location('W'); ax_rad.set_theta_direction(-1)
linea_radar, = ax_rad.plot([], [], 'bo-', linewidth=2, alpha=0.6)
canvas_radar = FigureCanvasTkAgg(fig_rad, master=right_frame); canvas_radar.get_tk_widget().pack(expand=True)

def update_radar():
    global thetas, radios, latest_distance, angulo
    theta_now = np.deg2rad(angulo)
    r_now = min(max(latest_distance, 0), max_distance)
    thetas.append(theta_now); radios.append(r_now)
    if len(thetas) > 20:
        thetas.pop(0); radios.pop(0)
    linea_radar.set_data(thetas, radios)
    canvas_radar.draw()
    root.after(100, update_radar)

root.after(100, update_plot)
root.after(500, update_radar)

# cierre ordenado
def on_close():
    _stop_event.set()
    try:
        if usbSerial and getattr(usbSerial, "is_open", False):
            usbSerial.close()
    except Exception:
        pass
    root.destroy()
    sys.exit(0)

root.protocol("WM_DELETE_WINDOW", on_close)
root.mainloop()
