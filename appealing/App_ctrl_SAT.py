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

# ---- ADICIONES MÍNIMAS: imports para eventos ----
import datetime
import os

plot_active = True

# Setup del serial
device = 'COM7'  # CAMBIAR según tu puerto
usbSerial = serial.Serial(device, 9600, timeout=1)

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

# === DATOS ORBITALES ===
orbit_x = []
orbit_y = []
orbit_lock = threading.Lock()

# Regex para parsear posición orbital
regex_orbit = re.compile(r"Position: \(X: ([\d\.-]+) m, Y: ([\d\.-]+) m, Z: ([\d\.-]+) m\)")

# ------------------ Registro de eventos (persistente) ------------------
EVENTOS_FILE = "eventos.txt"

def registrar_evento(tipo, detalles=""):
    """
    tipo: 'comando', 'alarma', 'observacion'
    detalles: string descriptivo
    Formato en fichero: 'YYYY-MM-DD HH:MM:SS|tipo|detalles'
    """
    ahora = datetime.datetime.now()
    fecha_hora = ahora.strftime("%Y-%m-%d %H:%M:%S")
    linea = f"{fecha_hora}|{tipo}|{detalles}\n"
    try:
        with open(EVENTOS_FILE, "a", encoding="utf-8") as f:
            f.write(linea)
    except Exception as e:
        print("Error registrando evento:", e)

def cargar_eventos():
    """Carga todos los eventos del fichero y devuelve lista de tuplas (datetime, tipo, desc)."""
    evs = []
    if not os.path.exists(EVENTOS_FILE):
        return evs
    try:
        with open(EVENTOS_FILE, "r", encoding="utf-8") as f:
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

def filtrar_eventos(tipo_filter=None, start_dt=None, end_dt=None):
    """Devuelve eventos filtrados (None = no filtrar)."""
    evs = cargar_eventos()
    out = []
    for dt, tipo, desc in evs:
        if tipo_filter and tipo_filter != "todos" and tipo != tipo_filter:
            continue
        if start_dt and dt < start_dt:
            continue
        if end_dt and dt > end_dt:
            continue
        out.append((dt, tipo, desc))
    return out

# Asegurar existencia del fichero de eventos
if not os.path.exists(EVENTOS_FILE):
    try:
        with open(EVENTOS_FILE, "w", encoding="utf-8") as f:
            f.write("")
    except Exception as e:
        print("No se pudo crear eventos.txt:", e)

# === FUNCIÓN CHECKSUM (AÑADIDA) ===
def calc_checksum(msg):
    """Calcula checksum XOR de un mensaje"""
    xor_sum = 0
    for char in msg:
        xor_sum ^= ord(char)
    hex_str = format(xor_sum, '02X')
    return hex_str

def send_command(command):
    """Envía comando con checksum al Ground Station"""
    checksum = calc_checksum(command)
    full_msg = f"{command}*{checksum}\n"
    try:
        usbSerial.write(full_msg.encode())
    except Exception as e:
        print("Error enviando serial:", e)
    print(f"Enviado: {full_msg.strip()}")
    # Registrar evento de tipo 'comando'
    registrar_evento("comando", command)

def read_serial():
    """Lee datos del puerto serial"""
    global plot_active, latest_distance, angulo, latest_temp_med, total_corrupted
    global orbit_x, orbit_y

    while True:
        try:
            linea = usbSerial.readline().decode('utf-8', errors='ignore').strip()
        except Exception as e:
            # Si hay un error en lectura no cierre el hilo
            print("Error leyendo serial:", e)
            time.sleep(0.1)
            continue

        if not linea:
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
                print(f"Orbital: X={x}, Y={y}")
            except ValueError:
                pass
            time.sleep(0.01)
            continue

        parts = linea.split(':')
        try:
            if len(parts) >= 2 and parts[0] in ('1','2','3','4','5','6','7','8','67','99'):
                idn = parts[0]
                
                if idn == '1':
                    if len(parts) >= 3:
                        try:
                            hum = int(parts[1]) / 100.0
                            temp = int(parts[2]) / 100.0
                            latest_data["temp"] = temp
                            latest_data["hum"] = hum
                            print(f"Temp: {temp:.2f}°C, Hum: {hum:.2f}%")
                        except ValueError:
                            pass

                elif idn == '2':
                    try:
                        latest_distance = int(parts[1])
                        print(f"Distancia: {latest_distance} mm")
                    except ValueError:
                        pass

                elif idn == '3':
                    plot_active = False
                    messagebox.showerror("Error transmisión", f"Error: {':'.join(parts[1:])}")
                    # registrar como alarma
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
                    # registrar alarma
                    registrar_evento("alarma", "Temperatura media >100°C")

                elif idn == '67':
                    pass

                elif idn == '99':
                    try:
                        corrupted = int(parts[1])
                        total_corrupted += corrupted
                        print(f"[CHECKSUM] Descartados: {corrupted} | Total: {total_corrupted}")
                        # registrar alarma
                        registrar_evento("alarma", f"Mensajes corruptos reportados: {corrupted}")
                    except ValueError:
                        pass

        except Exception as e:
            print("Parse error:", e)

        time.sleep(0.01)

threading.Thread(target=read_serial, daemon=True).start()

# === VENTANA ORBITAL (clase separada) ===
class VentanaOrbital:
    def __init__(self, parent):
        self.window = Toplevel(parent)
        self.window.title("Órbita Satelital")
        self.window.geometry("800x800")
        self.window.configure(bg="#1e1e2f")
        
        Label(self.window, text="Vista Orbital (Plano Ecuatorial)", 
              font=("Inter", 16, "bold"), bg="#1e1e2f", fg="white").pack(pady=10)
        
        # Gráfico
        self.fig, self.ax = plt.subplots(figsize=(7, 7))
        self.orbit_line, = self.ax.plot([], [], 'bo-', markersize=2, label='Órbita')
        self.last_point = self.ax.scatter([], [], color='red', s=50, label='Posición actual')
        
        # Círculo de la Tierra
        R_EARTH = 6371000
        earth = plt.Circle((0, 0), R_EARTH, color='orange', fill=False, linewidth=2, label='Tierra')
        self.ax.add_artist(earth)
        
        self.ax.set_xlim(-7e6, 7e6)
        self.ax.set_ylim(-7e6, 7e6)
        self.ax.set_aspect('equal', 'box')
        self.ax.set_xlabel('X (metros)')
        self.ax.set_ylabel('Y (metros)')
        self.ax.grid(True)
        self.ax.legend()
        
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
                
                # Auto-ajustar límites si es necesario
                max_coord = max(max(abs(x) for x in orbit_x), max(abs(y) for y in orbit_y))
                if max_coord > 6.5e6:
                    lim = max_coord * 1.1
                    self.ax.set_xlim(-lim, lim)
                    self.ax.set_ylim(-lim, lim)
        
        self.canvas.draw()
        self.window.after(500, self.update_plot)
    
    def on_close(self):
        self.active = False
        self.window.destroy()

# === GUI PRINCIPAL ===
window = Tk()
window.title("Control Satélite")
window.geometry("1800x800")
window.configure(bg="#1e1e2f")
window.resizable(False, False)

title_font = font.Font(family="Inter", size=22, weight="bold")
button_font = font.Font(family="Inter", size=14, weight="bold")
col_izq = "#1e292f"
col_der = "#31434d"

# Título
title_frame = Frame(window, bg="#1e1e2f")
title_frame.pack(pady=(20, 10))

Label(title_frame, text="Control Satélite", font=title_font, bg="#1e1e2f", fg="#ffffff").pack(side=LEFT, padx=20)

# Botón Ver Órbita
orbital_window = None
def open_orbital():
    global orbital_window
    if orbital_window is None or not orbital_window.active:
        orbital_window = VentanaOrbital(window)

Button(title_frame, text="🛰️ Ver Órbita", font=("Inter", 12, "bold"), command=open_orbital,
       bg="#6b8dd6", fg="white", bd=0, padx=15, pady=8).pack(side=LEFT)

# --- NUEVO: Botón Ver Eventos (en title_frame) ---
def abrir_vista_eventos():
    ev_win = Toplevel(window)
    ev_win.title("Eventos")
    ev_win.geometry("900x600")
    ev_win.configure(bg="#1e1e2f")

    # Filtros
    filtro_frame = Frame(ev_win, bg="#1e1e2f")
    filtro_frame.pack(pady=8, fill=X)

    Label(filtro_frame, text="Tipo:", bg="#1e1e2f", fg="white").pack(side=LEFT, padx=6)
    tipo_var = StringVar(value="todos")
    tipo_menu = OptionMenu(filtro_frame, tipo_var, "todos", "comando", "alarma", "observacion")
    tipo_menu.pack(side=LEFT, padx=6)

    Label(filtro_frame, text="Desde (dd-mm-YYYY HH:MM:SS):", bg="#1e1e2f", fg="white").pack(side=LEFT, padx=6)
    desde_entry = Entry(filtro_frame, width=20)
    desde_entry.pack(side=LEFT, padx=6)
    Label(filtro_frame, text="Hasta (dd-mm-YYYY HH:MM:SS):", bg="#1e1e2f", fg="white").pack(side=LEFT, padx=6)
    hasta_entry = Entry(filtro_frame, width=20)
    hasta_entry.pack(side=LEFT, padx=6)

    text_box = Text(ev_win, wrap=WORD, bg="#0f1720", fg="white")
    text_box.pack(expand=True, fill=BOTH, padx=8, pady=8)

    def aplicar_filtro():
        tipo = tipo_var.get()
        desde = desde_entry.get().strip()
        hasta = hasta_entry.get().strip()
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

    btnf = Button(filtro_frame, text="Aplicar filtro", command=aplicar_filtro, bg="#4b6cb7", fg="white")
    btnf.pack(side=LEFT, padx=6)

Button(title_frame, text="Ver eventos", font=("Inter", 12, "bold"), command=abrir_vista_eventos,
       bg="#6b8dd6", fg="white", bd=0, padx=15, pady=8).pack(side=LEFT)

# Entrada velocidad
entry = Entry(window, font=("Inter", 14), fg="#1e1e2f")
entry.pack(pady=20, ipadx=80, ipady=5)
placeholder = "Tiempo entre datos o evento"
entry.insert(0, placeholder)

def on_entry_click(event):
    if entry.get() == placeholder:
        entry.delete(0, END)
        entry.config(fg="black")

def on_focus_out(event):
    if entry.get() == "":
        entry.insert(0, placeholder)
        entry.config(fg="gray")

entry.bind("<FocusIn>", on_entry_click)
entry.bind("<FocusOut>", on_focus_out)

# CORREGIDO: función leer_vel con checksum
def leer_vel():
    vel_datos_raw = entry.get()
    if vel_datos_raw == placeholder or vel_datos_raw == "":
        messagebox.showerror("Error", "Introduzca valor entre 200-10000 ms")
        return
    try:
        vel_datos = int(vel_datos_raw)
        if 200 <= vel_datos <= 10000:
            send_command(f"1:{vel_datos}")
            messagebox.showinfo("OK", f"Velocidad: {vel_datos} ms")
        else:
            messagebox.showerror("Error", f"Fuera de rango: {vel_datos}")
    except ValueError:
        messagebox.showerror("Error", f"No numérico: {vel_datos_raw}")

Button(window, text="Validar", font=("Inter", 14, "bold"), command=leer_vel,
       bg="#4b6cb7", fg="white", bd=0, padx=20, pady=10).pack(pady=10)

# Frames
left_frame = Frame(window, bg=col_izq, width=900, height=600)
left_frame.pack(side=LEFT, fill=BOTH)
right_frame = Frame(window, bg=col_der, width=900, height=600)
right_frame.pack(side=RIGHT, fill=BOTH, expand=True)

left_frame.pack_propagate(0)
right_frame.pack_propagate(0)

# Botones izquierda
btn_frame_left = Frame(left_frame, bg=col_izq)
btn_frame_left.pack(pady=10)

def create_btn(master, text, command):
    return Button(master, text=text, command=command, font=button_font,
                  bg="#4b6cb7", fg="white", bd=0, padx=20, pady=15, width=18)

# CORREGIDO: funciones con checksum
def iniClick():
    global plot_active
    send_command("3:i")
    plot_active = True

def stopClick():
    global plot_active
    send_command("3:p")
    plot_active = False

def reanClick():
    global plot_active
    send_command("3:r")
    plot_active = True

create_btn(btn_frame_left, "Iniciar transmisión", iniClick).grid(row=0, column=0, padx=10)
create_btn(btn_frame_left, "Parar transmisión", stopClick).grid(row=0, column=1, padx=10)
create_btn(btn_frame_left, "Reanudar", reanClick).grid(row=0, column=2, padx=10)

# ----------------- Observaciones del usuario (GUI) -----------------
obs_frame = Frame(left_frame, bg=col_izq)
obs_frame.pack(pady=8)

Label(obs_frame, text="Observación:", bg=col_izq, fg="white", font=("Inter", 11)).grid(row=0, column=0, padx=6)
obs_entry = Entry(obs_frame, width=50, font=("Inter", 11))
obs_entry.grid(row=0, column=1, padx=6)

def agregar_observacion():
    text = obs_entry.get().strip()
    if not text:
        messagebox.showwarning("Observación vacía", "Escribe una observación antes de guardar.")
        return
    registrar_evento("observacion", text)
    messagebox.showinfo("OK", "Observación registrada")
    obs_entry.delete(0, END)

Button(obs_frame, text="Añadir observación", command=agregar_observacion,
       font=("Inter", 11), bg="#6b8dd6", fg="white").grid(row=0, column=2, padx=6)

# Gráfico temp/hum
fig_plot, ax_plot = plt.subplots(figsize=(7, 4.5))
ax_plot.set_ylim(0, 100)
ax_plot.set_title("Temperatura y Humedad")
line_temp, = ax_plot.plot(range(max_points), temps, label="Temperature")
line_hum, = ax_plot.plot(range(max_points), hums, label="Humidity")
line_med, = ax_plot.plot(range(max_points), temps_med, label="Avg. temp")
ax_plot.legend()
canvas_plot = FigureCanvasTkAgg(fig_plot, master=left_frame)
canvas_plot.get_tk_widget().pack(pady=20)

def update_plot():
    temps.append(latest_data["temp"])
    hums.append(latest_data["hum"])
    temps_med.append(latest_temp_med)

    line_temp.set_visible(plot_active)
    line_hum.set_visible(plot_active)
    line_med.set_visible(plot_active)

    line_temp.set_ydata(temps)
    line_hum.set_ydata(hums)
    line_med.set_ydata(temps_med)

    ax_plot.relim()
    ax_plot.autoscale_view()
    canvas_plot.draw()
    window.after(100, update_plot)

# Botones derecha
btn_frame_right = Frame(right_frame, bg=col_der)
btn_frame_right.pack(pady=10)

# CORREGIDO: funciones con checksum
def os_man():
    send_command("4:m")

def os_auto():
    send_command("4:a")

create_btn(btn_frame_right, "OS Auto", os_auto).grid(row=0, column=0, padx=10)
create_btn(btn_frame_right, "OS Manual", os_man).grid(row=0, column=1, padx=10)

# Gráfico radar
fig, ax_rad = plt.subplots(subplot_kw={'polar': True}, figsize=(7,4.5))
max_distance = 500
ax_rad.set_ylim(0, max_distance)
ax_rad.set_thetamin(0)
ax_rad.set_thetamax(180)
ax_rad.set_theta_zero_location('W')
ax_rad.set_theta_direction(-1)

linea_radar, = ax_rad.plot([], [], 'bo-', linewidth=2, alpha=0.6)
canvas_radar = FigureCanvasTkAgg(fig, master=right_frame)
canvas_radar.get_tk_widget().pack(expand=True)

def update_radar():
    global latest_distance, angulo, thetas, radios
    theta_now = np.deg2rad(angulo)
    r_now = min(max(latest_distance, 0), max_distance)
    thetas.append(theta_now)
    radios.append(r_now)
    if len(thetas) > 20:
        thetas.pop(0)
        radios.pop(0)
    linea_radar.set_data(thetas, radios)
    canvas_radar.draw()
    window.after(100, update_radar)

window.after(100, update_plot)
window.after(500, update_radar)

def on_close():
    try:
        usbSerial.close()
    except:
        pass
    window.destroy()
    exit()

window.protocol("WM_DELETE_WINDOW", on_close)
window.mainloop()
