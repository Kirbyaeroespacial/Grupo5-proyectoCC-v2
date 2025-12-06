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

import datetime
import os

plot_active = True

# Setup del serial
device = 'COM7'  # CAMBIAR según tu puerto
usbSerial = None

# Intento de apertura del puerto serie
try:
    usbSerial = serial.Serial(device, 9600, timeout=1)
    print(f"Puerto serial abierto: {device}")
except Exception as e:
    usbSerial = None
    print(f"No se pudo abrir {device}: {e}")

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

# === ESTADO DEL PANEL SOLAR ===
panel_state = 0
panel_lock = threading.Lock()

# Estados de transmisión para cambio de color de botones
transmission_state = "stopped"  # "running", "stopped", "paused"

# Regex para parsear datos
regex_orbit = re.compile(r"Position: \(X: ([\d\.-]+) m, Y: ([\d\.-]+) m, Z: ([\d\.-]+) m\)")
regex_panel = re.compile(r"Panel:(\d+)")

# Registro de eventos
EVENTOS_FILE = "eventos.txt"

def registrar_evento(tipo, detalles=""):
    ahora = datetime.datetime.now()
    fecha_hora = ahora.strftime("%Y-%m-%d %H:%M:%S")
    linea = f"{fecha_hora}|{tipo}|{detalles}\n"
    try:
        with open(EVENTOS_FILE, "a", encoding="utf-8") as f:
            f.write(linea)
    except Exception as e:
        print("Error registrando evento:", e)

def cargar_eventos():
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

if not os.path.exists(EVENTOS_FILE):
    try:
        with open(EVENTOS_FILE, "w", encoding="utf-8") as f:
            f.write("")
    except Exception as e:
        print("No se pudo crear eventos.txt:", e)

def calc_checksum(msg):
    xor_sum = 0
    for char in msg:
        xor_sum ^= ord(char)
    hex_str = format(xor_sum, '02X')
    return hex_str

def send_command(command):
    if usbSerial is None:
        messagebox.showwarning("Sin conexión", "No hay puerto serial conectado")
        return
    checksum = calc_checksum(command)
    full_msg = f"{command}*{checksum}\n"
    try:
        usbSerial.write(full_msg.encode())
    except Exception as e:
        print("Error enviando serial:", e)
    print(f"Enviado: {full_msg.strip()}")
    registrar_evento("comando", command)

def read_serial():
    global plot_active, latest_distance, angulo, latest_temp_med, total_corrupted
    global orbit_x, orbit_y, panel_state
    if usbSerial is None:
        return

    while True:
        try:
            linea = usbSerial.readline().decode('utf-8', errors='ignore').strip()
        except Exception as e:
            print("Error leyendo serial:", e)
            time.sleep(0.1)
            continue

        if not linea:
            time.sleep(0.01)
            continue

        # === PARSEO DE POSICIÓN ORBITAL ===
        match_orbit = regex_orbit.search(linea)
        if match_orbit:
            try:
                x = float(match_orbit.group(1))
                y = float(match_orbit.group(2))
                with orbit_lock:
                    orbit_x.append(x)
                    orbit_y.append(y)
                print(f"Orbital: X={x}, Y={y}")
            except ValueError:
                pass
            time.sleep(0.01)
            continue

        # === PARSEO DE ESTADO DEL PANEL SOLAR ===
        match_panel = regex_panel.search(linea)
        if match_panel:
            try:
                new_state = int(match_panel.group(1))
                with panel_lock:
                    old_state = panel_state
                    panel_state = new_state
                
                if new_state != old_state:
                    estado_texto = {
                        0: "RETRAÍDO (0%)",
                        40: "DESPLEGADO 40%",
                        60: "DESPLEGADO 60%",
                        100: "TOTALMENTE DESPLEGADO (100%)"
                    }
                    msg = f"Panel solar: {estado_texto.get(new_state, f'{new_state}%')}"
                    print(f"🛰️ {msg}")
                    messagebox.showinfo("Estado Panel Solar", msg)
                    registrar_evento("alarma", msg)
            except ValueError:
                pass
            time.sleep(0.01)
            continue

        # === PARSEO DE PROTOCOLOS ESTÁNDAR ===
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
                    pass

                elif idn == '99':
                    try:
                        corrupted = int(parts[1])
                        total_corrupted += corrupted
                        print(f"[CHECKSUM] Descartados: {corrupted} | Total: {total_corrupted}")
                        registrar_evento("alarma", f"Mensajes corruptos reportados: {corrupted}")
                    except ValueError:
                        pass

        except Exception as e:
            print("Parse error:", e)

        time.sleep(0.01)

if usbSerial is not None:
    threading.Thread(target=read_serial, daemon=True).start()
else:
    print("Modo simulación/solo GUI: leyendo serial deshabilitado.")

# === GUI PRINCIPAL ===
window = Tk()
window.title("Control Satélite")
window.geometry("1600x900")
window.configure(bg="navy")
window.resizable(False, False)

# Fuentes
title_font = font.Font(family="Arial", size=28, weight="bold")
subtitle_font = font.Font(family="Arial", size=12, weight="bold")
button_font = font.Font(family="Arial", size=11, weight="bold")

# === TÍTULO PRINCIPAL ===
title_label = Label(window, text="🛰️ CONTROL SATÉLITE", font=title_font, 
                    bg="navy", fg="white")
title_label.pack(pady=15)

# === CONTENEDOR DE GRÁFICOS (3 columnas) ===
graphs_frame = Frame(window, bg="navy")
graphs_frame.pack(pady=10, padx=20)

# Configurar matplotlib con fondo oscuro
plt.style.use('dark_background')

# === GRÁFICO 1: ÓRBITA SATELITAL ===
orbit_frame = Frame(graphs_frame, bg="navy")
orbit_frame.grid(row=0, column=0, padx=10)

Label(orbit_frame, text="🌍 Órbita Satelital", font=subtitle_font, 
      bg="navy", fg="white").pack()

fig_orbit, ax_orbit = plt.subplots(figsize=(5, 4.5), facecolor='#0a0a2e')
ax_orbit.set_facecolor('#0a0a2e')
orbit_line, = ax_orbit.plot([], [], 'cyan', linewidth=1.5, label='Trayectoria')
orbit_point = ax_orbit.scatter([], [], color='red', s=80, marker='o', label='Satélite')

R_EARTH = 6371000
earth_circle = plt.Circle((0, 0), R_EARTH, color='green', fill=False, linewidth=2, label='Tierra')
ax_orbit.add_artist(earth_circle)

ax_orbit.set_xlim(-7e6, 7e6)
ax_orbit.set_ylim(-7e6, 7e6)
ax_orbit.set_aspect('equal', 'box')
ax_orbit.set_xlabel('X (m)', color='white')
ax_orbit.set_ylabel('Y (m)', color='white')
ax_orbit.tick_params(colors='white')
ax_orbit.grid(True, alpha=0.3)
ax_orbit.legend(loc='upper right', fontsize=8)

canvas_orbit = FigureCanvasTkAgg(fig_orbit, master=orbit_frame)
canvas_orbit.get_tk_widget().pack()

# === GRÁFICO 2: RADAR ===
radar_frame = Frame(graphs_frame, bg="navy")
radar_frame.grid(row=0, column=1, padx=10)

Label(radar_frame, text="📡 Radar de Distancia", font=subtitle_font, 
      bg="navy", fg="white").pack()

fig_radar, ax_radar = plt.subplots(subplot_kw={'polar': True}, figsize=(5, 4.5), facecolor='#0a0a2e')
ax_radar.set_facecolor('#0a0a2e')
max_distance = 500
ax_radar.set_ylim(0, max_distance)
ax_radar.set_thetamin(0)
ax_radar.set_thetamax(180)
ax_radar.set_theta_zero_location('W')
ax_radar.set_theta_direction(-1)
ax_radar.set_xlabel('Distancia (mm)', color='white')
ax_radar.tick_params(colors='white')
ax_radar.grid(True, alpha=0.3)

linea_radar, = ax_radar.plot([], [], 'lime', linewidth=2, marker='o', markersize=4)

canvas_radar = FigureCanvasTkAgg(fig_radar, master=radar_frame)
canvas_radar.get_tk_widget().pack()

# === GRÁFICO 3: TEMPERATURA Y HUMEDAD ===
temp_frame = Frame(graphs_frame, bg="navy")
temp_frame.grid(row=0, column=2, padx=10)

Label(temp_frame, text="🌡️ Temperatura y Humedad", font=subtitle_font, 
      bg="navy", fg="white").pack()

fig_temp, ax_temp = plt.subplots(figsize=(5, 4.5), facecolor='#0a0a2e')
ax_temp.set_facecolor('#0a0a2e')
ax_temp.set_ylim(0, 100)
ax_temp.set_xlabel('Tiempo (muestras)', color='white')
ax_temp.set_ylabel('Valor', color='white')
ax_temp.tick_params(colors='white')
ax_temp.grid(True, alpha=0.3)

line_temp, = ax_temp.plot(range(max_points), temps, 'red', linewidth=2, label='Temp (°C)')
line_hum, = ax_temp.plot(range(max_points), hums, 'cyan', linewidth=2, label='Hum (%)')
line_med, = ax_temp.plot(range(max_points), temps_med, 'yellow', linewidth=2, label='Temp Media (°C)')
ax_temp.legend(loc='upper right', fontsize=8)

canvas_temp = FigureCanvasTkAgg(fig_temp, master=temp_frame)
canvas_temp.get_tk_widget().pack()

# === CONTENEDOR DE CONTROLES (3 columnas debajo de gráficos) ===
controls_frame = Frame(window, bg="navy")
controls_frame.pack(pady=20, padx=20)

# === COLUMNA 1: CONTROLES DE TRANSMISIÓN ===
transmission_frame = Frame(controls_frame, bg="navy")
transmission_frame.grid(row=0, column=0, padx=20, sticky=N)

Label(transmission_frame, text="📡 Transmisión", font=subtitle_font, 
      bg="navy", fg="white").pack(pady=5)

# Botones de transmisión
btn_iniciar = Button(transmission_frame, text="▶ Iniciar Transmisión", 
                     font=button_font, bg="royalblue", fg="white", 
                     relief=FLAT, bd=0, padx=20, pady=10, width=20)
btn_iniciar.pack(pady=5)

btn_parar = Button(transmission_frame, text="⏸ Parar Transmisión", 
                   font=button_font, bg="royalblue", fg="white", 
                   relief=FLAT, bd=0, padx=20, pady=10, width=20)
btn_parar.pack(pady=5)

btn_reanudar = Button(transmission_frame, text="⏯ Reanudar Transmisión", 
                      font=button_font, bg="royalblue", fg="white", 
                      relief=FLAT, bd=0, padx=20, pady=10, width=20)
btn_reanudar.pack(pady=5)

# Frame para intervalo de datos con botón al lado
interval_frame = Frame(transmission_frame, bg="navy")
interval_frame.pack(pady=(15, 0))

Label(interval_frame, text="⏱️ Intervalo (ms):", 
      font=("Arial", 10), bg="navy", fg="white").grid(row=0, column=0, columnspan=2, pady=(0, 5))

entry_tiempo = Entry(interval_frame, font=("Arial", 12), width=12)
entry_tiempo.grid(row=1, column=0, padx=(0, 5), ipady=3)
entry_tiempo.insert(0, "200-10000")

# DEFINIR función leer_vel ANTES de crear el botón
def leer_vel():
    vel_datos_raw = entry_tiempo.get()
    try:
        vel_datos = int(vel_datos_raw)
        if 200 <= vel_datos <= 10000:
            send_command(f"1:{vel_datos}")
            messagebox.showinfo("✓ Validado", f"Intervalo configurado: {vel_datos} ms")
        else:
            messagebox.showerror("Error", f"Valor fuera de rango (200-10000): {vel_datos}")
    except ValueError:
        messagebox.showerror("Error", "Introduce un valor numérico válido")

btn_validar = Button(interval_frame, text="✓ Validar", 
                     font=("Arial", 10, "bold"), bg="deepskyblue", fg="white", 
                     relief=FLAT, bd=0, padx=15, pady=10, command=leer_vel)
btn_validar.grid(row=1, column=1, padx=(5, 0))

# === COLUMNA 2: CONTROLES DE MODO ===
mode_frame = Frame(controls_frame, bg="navy")
mode_frame.grid(row=0, column=1, padx=20, sticky=N)

Label(mode_frame, text="⚙️ Modo Operación", font=subtitle_font, 
      bg="navy", fg="white").pack(pady=5)

btn_auto = Button(mode_frame, text="🔄 Modo Automático", 
                  font=button_font, bg="royalblue", fg="white", 
                  relief=FLAT, bd=0, padx=20, pady=10, width=20)
btn_auto.pack(pady=10)

btn_manual = Button(mode_frame, text="🎮 Modo Manual", 
                    font=button_font, bg="royalblue", fg="white", 
                    relief=FLAT, bd=0, padx=20, pady=10, width=20)
btn_manual.pack(pady=10)

# === COLUMNA 3: EVENTOS Y PANEL SOLAR ===
events_frame = Frame(controls_frame, bg="navy")
events_frame.grid(row=0, column=2, padx=20, sticky=N)

Label(events_frame, text="📝 Observaciones", font=subtitle_font, 
      bg="navy", fg="white").pack(pady=5)

# Entrada de observación
obs_subframe = Frame(events_frame, bg="navy")
obs_subframe.pack(pady=5)

Label(obs_subframe, text="Nota:", font=("Arial", 10), 
      bg="navy", fg="white").pack(side=LEFT, padx=5)
obs_entry = Entry(obs_subframe, font=("Arial", 10), width=25)
obs_entry.pack(side=LEFT, padx=5, ipady=3)

btn_add_obs = Button(events_frame, text="➕ Añadir Observación", 
                     font=button_font, bg="deepskyblue", fg="white", 
                     relief=FLAT, bd=0, padx=20, pady=8, width=20)
btn_add_obs.pack(pady=5)

# Botón ver eventos
btn_ver_eventos = Button(events_frame, text="📋 Ver Eventos", 
                         font=button_font, bg="royalblue", fg="white", 
                         relief=FLAT, bd=0, padx=20, pady=8, width=20)
btn_ver_eventos.pack(pady=10)

# Indicador de panel solar (SIN BORDE)
panel_frame = Frame(events_frame, bg="navy")
panel_frame.pack(pady=10)

Label(panel_frame, text="☀️ Estado Panel Solar", font=("Arial", 10, "bold"), 
      bg="navy", fg="white").pack(pady=3)

panel_label = Label(panel_frame, text="RETRAÍDO", font=("Arial", 12, "bold"),
                   bg="#ff6b6b", fg="white", padx=15, pady=5)
panel_label.pack(pady=5, padx=10)

# === FUNCIONES DE BOTONES ===

def update_transmission_buttons():
    """Actualiza colores de botones según estado"""
    if transmission_state == "running":
        btn_iniciar.config(bg="mediumslateblue")
        btn_parar.config(bg="royalblue")
        btn_reanudar.config(bg="royalblue")
    elif transmission_state == "stopped":
        btn_iniciar.config(bg="royalblue")
        btn_parar.config(bg="royalblue")
        btn_reanudar.config(bg="royalblue")
    elif transmission_state == "paused":
        btn_iniciar.config(bg="royalblue")
        btn_parar.config(bg="royalblue")
        btn_reanudar.config(bg="mediumslateblue")

def iniClick():
    global plot_active, transmission_state
    send_command("3:i")
    plot_active = True
    transmission_state = "running"
    update_transmission_buttons()

def stopClick():
    global plot_active, transmission_state
    send_command("3:p")
    plot_active = False
    transmission_state = "stopped"
    update_transmission_buttons()

def reanClick():
    global plot_active, transmission_state
    send_command("3:r")
    plot_active = True
    transmission_state = "paused"
    update_transmission_buttons()

def os_auto():
    send_command("4:a")

def os_manual():
    send_command("4:m")

def agregar_observacion():
    text = obs_entry.get().strip()
    if not text:
        messagebox.showwarning("Observación vacía", "Escribe una observación antes de guardar.")
        return
    registrar_evento("observacion", text)
    messagebox.showinfo("✓ Guardado", "Observación registrada correctamente")
    obs_entry.delete(0, END)

def abrir_vista_eventos():
    ev_win = Toplevel(window)
    ev_win.title("📋 Registro de Eventos")
    ev_win.geometry("1000x600")
    ev_win.configure(bg="navy")

    filtro_frame = Frame(ev_win, bg="navy")
    filtro_frame.pack(pady=10, fill=X)

    Label(filtro_frame, text="Tipo:", bg="navy", fg="white", font=("Arial", 10)).pack(side=LEFT, padx=6)
    tipo_var = StringVar(value="todos")
    tipo_menu = OptionMenu(filtro_frame, tipo_var, "todos", "comando", "alarma", "observacion")
    tipo_menu.config(bg="royalblue", fg="white")
    tipo_menu.pack(side=LEFT, padx=6)

    Label(filtro_frame, text="Desde (dd-mm-YYYY HH:MM:SS):", 
          bg="navy", fg="white", font=("Arial", 10)).pack(side=LEFT, padx=6)
    desde_entry = Entry(filtro_frame, width=20)
    desde_entry.pack(side=LEFT, padx=6)
    
    Label(filtro_frame, text="Hasta (dd-mm-YYYY HH:MM:SS):", 
          bg="navy", fg="white", font=("Arial", 10)).pack(side=LEFT, padx=6)
    hasta_entry = Entry(filtro_frame, width=20)
    hasta_entry.pack(side=LEFT, padx=6)

    text_box = Text(ev_win, wrap=WORD, bg="#0a0a2e", fg="white", font=("Courier", 10))
    text_box.pack(expand=True, fill=BOTH, padx=10, pady=10)

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

    btnf = Button(filtro_frame, text="Aplicar filtro", command=aplicar_filtro, 
                  bg="deepskyblue", fg="white", font=button_font)
    btnf.pack(side=LEFT, padx=6)

# Asignar comandos a botones
btn_iniciar.config(command=iniClick)
btn_parar.config(command=stopClick)
btn_reanudar.config(command=reanClick)
btn_auto.config(command=os_auto)
btn_manual.config(command=os_manual)
btn_add_obs.config(command=agregar_observacion)
btn_ver_eventos.config(command=abrir_vista_eventos)

# === FUNCIONES DE ACTUALIZACIÓN DE GRÁFICOS ===

def update_orbit_plot():
    with orbit_lock:
        if len(orbit_x) > 0:
            orbit_line.set_data(orbit_x, orbit_y)
            orbit_point.set_offsets([[orbit_x[-1], orbit_y[-1]]])
            
            max_coord = max(max(abs(x) for x in orbit_x), max(abs(y) for y in orbit_y))
            if max_coord > 6.5e6:
                lim = max_coord * 1.1
                ax_orbit.set_xlim(-lim, lim)
                ax_orbit.set_ylim(-lim, lim)
    
    canvas_orbit.draw()
    window.after(500, update_orbit_plot)

def update_radar_plot():
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
    window.after(100, update_radar_plot)

def update_temp_plot():
    temps.append(latest_data["temp"])
    hums.append(latest_data["hum"])
    temps_med.append(latest_temp_med)

    line_temp.set_visible(plot_active)
    line_hum.set_visible(plot_active)
    line_med.set_visible(plot_active)

    line_temp.set_ydata(temps)
    line_hum.set_ydata(hums)
    line_med.set_ydata(temps_med)

    ax_temp.relim()
    ax_temp.autoscale_view()
    canvas_temp.draw()
    window.after(100, update_temp_plot)

def update_panel_indicator():
    with panel_lock:
        state = panel_state
    
    estado_texto = {
        0: "RETRAÍDO",
        40: "40% DESPLEGADO",
        60: "60% DESPLEGADO",
        100: "100% DESPLEGADO"
    }
    
    colores = {
        0: "#ff6b6b",
        40: "#ffd93d",
        60: "#6bcf7f",
        100: "#51cf66"
    }
    
    panel_label.config(
        text=estado_texto.get(state, f"{state}%"),
        bg=colores.get(state, "#888888")
    )
    
    window.after(500, update_panel_indicator)

# Iniciar actualizaciones
window.after(100, update_temp_plot)
window.after(500, update_radar_plot)
window.after(500, update_orbit_plot)
window.after(500, update_panel_indicator)

def on_close():
    try:
        if usbSerial:
            usbSerial.close()
    except:
        pass
    window.destroy()
    exit()

window.protocol("WM_DELETE_WINDOW", on_close)
window.mainloop()
