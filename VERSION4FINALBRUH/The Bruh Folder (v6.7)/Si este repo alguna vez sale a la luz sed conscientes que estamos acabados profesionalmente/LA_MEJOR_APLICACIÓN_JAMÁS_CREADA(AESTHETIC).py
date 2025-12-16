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
from PIL import Image, ImageTk

plot_active = True

#Configuración interfaz
window = Tk()
window.title("Control Satélite")
resolx = 1600
resoly = 950
resol_main = f"{resolx}x{resoly}"
bg_main = "navy"
tf = "white"
btcl = "royalblue"
window.geometry(resol_main)
window.resizable(False, False)
window.configure(bg=bg_main)

#Fuentes
title_font = font.Font(family="Arial", size=28, weight="bold")
subtitle_font = font.Font(family="Arial", size=12, weight="bold")
button_font = font.Font(family="Arial", size=11, weight="bold")
font_std = font.Font(family="Arial", size=10)

#Matplotlib
plt.style.use('dark_background')

#Serial Setup
def detectar_puerto_automatico():
    import serial.tools.list_ports
    
    puertos = serial.tools.list_ports.comports()
    
    descripciones_validas = [
        'Arduino',
        'CH340',
        'USB Serial',
        'USB-SERIAL',
        'CP210',
        'FTDI',
        'USB2.0-Serial'
    ]
    
    print("🔍 Buscando puertos serie disponibles...")
    for puerto in puertos:
        print(f"   📌 Encontrado: {puerto.device} - {puerto.description}")
        for desc in descripciones_validas:
            if desc.lower() in puerto.description.lower():
                print(f"   ✓ Puerto compatible detectado: {puerto.device}")
                return puerto.device
    
    print("   ⚠️ No se encontró ningún puerto compatible automáticamente")
    return None

usbSerial = None
device = detectar_puerto_automatico()

if device is None:
    comm_port = '13' #Para debug
    device = f'COM{comm_port}'
    print(f"⚙️ Usando puerto manual: {device}")

try:
    usbSerial = serial.Serial(device, 9600, timeout=1)
    print(f"✓ Se ha abierto el puerto {device}")
except Exception as e:
    usbSerial = None
    print(f"✗ No se ha podido abrir el puerto serie {device}")
    print(f"   Error: {e}")
    print(f"   💡 Verifica que el dispositivo esté conectado y no esté en uso por otro programa")

#Búfers
max_points = 100
temps = deque([0]*max_points, maxlen=max_points)
hums = deque([0]*max_points, maxlen=max_points)
tempmeds = deque(maxlen = 10)
temps_med = deque([0]*max_points, maxlen=max_points)
latest_data = {"temp": 0, "hum": 0}
angulo = 90
latest_temp_med = 0

latest_distance = 0
thetas = []
radios = []

orbit_x = []
orbit_y = []
orbit_z = []
orbit_lock = threading.Lock()
ground_track_lat = []
ground_track_lon = []
ground_track_lock = threading.Lock()

regex_orbit = re.compile(r"Position: \(X: ([\d\.-]+) m, Y: ([\d\.-]+) m, Z: ([\d\.-]+) m\)")
regex_panel = re.compile(r"Panel:(\d+)")

total_corrupted = 0

panel_state = 0
panel_lock = threading.Lock()

transmission_state = "stopped"

local_med = False


EVENTOS_FILE = "eventos.txt"

def xyz_to_latlon(x, y, z):
    r = np.sqrt(x**2 + y**2 + z**2)
    if r == 0:
        return 0, 0
    lat = np.degrees(np.arcsin(z/r))
    lon = np.degrees(np.arctan2(y, x))
    return lat, lon

def registrar_evento(tipo, detalles=""):
    ahora = datetime.datetime.now()
    fecha_hora = ahora.strftime("%Y-%m-%d %H:%M:%S")
    linea = f"{fecha_hora}|{tipo}|{detalles}\n"
    try:
        with open(EVENTOS_FILE, "a", encoding="utf-8") as f:
            f.write(linea)
    except Exception as e:
        print("Error registrando evento:", e)

def prot_orbit(match_orbit):
    global x, y, z, orbit_x, orbit_y, orbit_z, lat, lon, ground_track_lat, ground_track_lon

    try:
        x = float(match_orbit.group(1))
        y = float(match_orbit.group(2))
        z = float(match_orbit.group(3))

        with orbit_lock:
            orbit_x.append(x)
            orbit_y.append(y)
            orbit_z.append(z)
        
        lat, lon = xyz_to_latlon(x, y, z)

        with ground_track_lock:
            ground_track_lat.append(lat)
            ground_track_lon.append(lon)

            if len(ground_track_lat) > 600:
                ground_track_lat.pop(0)
                ground_track_lon.pop(0)
        
        print(f"Orbital: X={x:.0f}, Y={y:.0f}, Z={z:.0f} | Lat={lat:.2f}°, Lon={lon:.2f}°")
    except ValueError:
        pass
    time.sleep(0.01)

def prot_solar(match_panel):
    global new_state, old_state, panel_state
    try:
        new_state = int(match_panel.group(1))
        with panel_lock:
            old_state = panel_state
            panel_state = new_state
        if new_state != old_state:
            estado_texto = {0: "RETRAÍDO (0%)", 40: "DESPLEGADO 40%", 60: "DESPLEGADO 60%", 100: "TOTALMENTE DESPLEGADO (100%)"}
            msg = f"Panel solar: {estado_texto.get(new_state, f'{new_state}%')}"
            print(f"🛰️ {msg}")
            window.after(0, lambda: messagebox.showinfo("Estado Panel Solar", msg))
            registrar_evento("alarma", msg)
    except ValueError:
        pass
    time.sleep(0.01)

def prot1(parts):
    global hum, temp, latest_data, tempmeds
    try:
        if len(parts) >= 3:
            hum = int(parts[1]) / 100.0
            temp = int(parts[2]) / 100.0
            latest_data["temp"] = temp
            latest_data["hum"] = hum
            tempmeds.append(temp)
            print(f"Temp: {temp:.2f}ºC, Hum: {hum:.2f}%")
    except ValueError:
        pass

def prot2(parts):
    global latest_distance
    try:
        latest_distance = int(parts[1])
        print(f"Distancia: {latest_distance} mm")
    except ValueError:
        pass

def prot3(parts):
    global plot_active
    plot_active = False
    msg = f"Error: {':'.join(parts[1:])}"
    window.after(0, lambda: messagebox.showerror("Error transmisión", msg))
    registrar_evento("alarma", "Error transmisión: " + ":".join(parts[1:]))

def prot4():
    window.after(0, lambda: messagebox.showerror("Error sensor", "Error en sensor temp/hum"))
    registrar_evento("alarma", "Error sensor temp/hum")

def prot5():
    window.after(0, lambda: messagebox.showerror("Error sensor", "Error en sensor distancia"))
    registrar_evento("alarma", "Error sensor distancia")

def prot6(parts):
    global angulo
    try:
        angulo = int(parts[1])
    except ValueError:
        window.after(0, lambda: messagebox.showerror("Error ángulo", "Valor incorrecto"))

def prot7(parts):
    global latest_temp_med
    try:
        latest_temp_med = int(parts[1]) / 100.0
    except ValueError:
        pass

def prot8():
    window.after(0, lambda: messagebox.showinfo("Alta temperatura!", "¡PELIGRO! Temp media >100°C"))
    registrar_evento("alarma", "Temperatura media >100°C")

def corrupt_chcksum(parts):
    global total_corrupted
    try:
        corrupted = int(parts[1])
        total_corrupted += corrupted
        print(f"[CHECKSUM] Descartados: {corrupted} | Total: {total_corrupted}")
        registrar_evento("alarma", f"Mensajes corruptos reportados: {corrupted}")
    except ValueError:
        pass

def read_serial():
    global plot_active, latest_distance, angulo, latest_temp_med, total_corrupted, tempmeds
    global orbit_x, orbit_y, orbit_z, panel_state
    global ground_track_lat, ground_track_lon

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

        match_orbit = regex_orbit.search(linea)
        match_panel = regex_panel.search(linea)
        parts = linea.split(":")

        if match_orbit:
            prot_orbit(match_orbit)
            continue
        if match_panel:
            prot_solar(match_panel)
            continue
        try:
            if len(parts) >= 2 and parts[0] in ('1','2','3','4','5','6','7','8','67','99'):
                idn = parts[0]

                if idn == '1':
                    prot1(parts)
                elif idn == '2':
                    prot2(parts)
                elif idn == '3':
                    prot3(parts)
                elif idn == '4':
                    prot4()
                elif idn == '5':
                    prot5()
                elif idn == '6':
                    prot6(parts)
                elif idn == '7':
                    prot7(parts)
                elif idn == '8':
                    prot8()
                elif idn == '67':
                    pass
                elif idn == '99':
                    corrupt_chcksum(parts)
        except Exception as e:
            print("Parse error:", e)

        time.sleep(0.01)

if usbSerial is not None:
    threading.Thread(target=read_serial, daemon=True).start()
else:
    print("Modo simulación/solo GUI: leyendo serial deshabilitado.")

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

if not os.path.exists(EVENTOS_FILE):
    try:
        with open(EVENTOS_FILE, "w", encoding="utf-8") as f:
            f.write("")
    except Exception as e:
        print("No se pudo crear eventos.txt:", e)

def update_transmission_buttons():
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
    if not text or text == "Escribe aquí...":
        messagebox.showwarning("Observación vacía", "Escribe una observación antes de guardar.")
        return
    registrar_evento("observacion", text)
    messagebox.showinfo("✓ Guardado", "Observación registrada correctamente")
    obs_entry.delete(0, END)
    obs_entry.insert(0, "Escribe aquí...")
    obs_entry.config(fg="gray")

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

def abrir_ground_track():
    gt_win = Toplevel(window)
    gt_win.title("🗺️ Ground Track - Traza Terrestre")
    gt_win.geometry("900x700")
    gt_win.configure(bg="navy")
    
    Label(gt_win, text="🛰️ GROUND TRACK", font=title_font, 
          bg="navy", fg="white").pack(pady=15)
    
    plot_frame = Frame(gt_win, bg="navy")
    plot_frame.pack(expand=True, fill=BOTH, padx=20, pady=10)
    
    fig_gt_win, ax_gt_win = plt.subplots(figsize=(10, 7), facecolor='#0a0a2e')
    ax_gt_win.set_facecolor('#1a1a3e')
    
    try:
        from matplotlib import image as mpimg
        map_img = mpimg.imread(r'D:\Uni\CC\Git Proyecto Computación\Grupo5-proyectoCC-v2\src\v3\mapa.jpg')
        ax_gt_win.imshow(map_img, extent=[-180, 180, -90, 90], aspect='auto', alpha=0.6, zorder=0)
        print("✓ Mapa cargado: mapa.jpg")
    except FileNotFoundError:
        print("No se encontró mapa.jpg - usando fondo sin mapa")
        print("   Coloca una imagen 'mapa.jpg' en la carpeta del script")
    except Exception as e:
        print(f"Error cargando mapa: {e}")
    
    ax_gt_win.set_xlim(-180, 180)
    ax_gt_win.set_ylim(-90, 90)
    ax_gt_win.set_xlabel('Longitud (°)', color='white', fontsize=12)
    ax_gt_win.set_ylabel('Latitud (°)', color='white', fontsize=12)
    ax_gt_win.tick_params(colors='white', labelsize=10)
    ax_gt_win.grid(True, alpha=0.3, linestyle='--', linewidth=0.5, color='cyan', zorder=1)
    
    ax_gt_win.axhline(0, color='yellow', linewidth=1.5, alpha=0.7, linestyle='--', label='Ecuador', zorder=2)
    ax_gt_win.axvline(0, color='orange', linewidth=1.5, alpha=0.7, linestyle='--', label='Meridiano 0°', zorder=2)
    
    gt_line_win, = ax_gt_win.plot([], [], 'cyan', linewidth=2.5, alpha=0.8, label='Trayectoria', zorder=3)
    gt_point_win = ax_gt_win.scatter([], [], color='red', s=150, marker='o', 
                                     edgecolors='yellow', linewidths=3, label='Posición actual', zorder=4)
    
    ax_gt_win.legend(loc='upper right', fontsize=11, framealpha=0.9)
    ax_gt_win.set_title('Traza Terrestre del Satélite', color='white', fontsize=14, weight='bold')
    
    canvas_gt_win = FigureCanvasTkAgg(fig_gt_win, master=plot_frame)
    canvas_gt_win.get_tk_widget().pack(expand=True, fill=BOTH)
    
    info_label = Label(gt_win, text="Esperando datos del satélite...", 
                      font=("Arial", 11), bg="navy", fg="white")
    info_label.pack(pady=5)
    
    def update_gt_window():
        if not gt_win.winfo_exists():
            return
        
        with ground_track_lock:
            if len(ground_track_lat) > 0:
                gt_line_win.set_data(ground_track_lon, ground_track_lat)
                gt_point_win.set_offsets([[ground_track_lon[-1], ground_track_lat[-1]]])
                
                lat = ground_track_lat[-1]
                lon = ground_track_lon[-1]
                info_label.config(text=f"Posición actual: Lat {lat:.2f}° | Lon {lon:.2f}° | Puntos: {len(ground_track_lat)}")
        
        canvas_gt_win.draw()
        gt_win.after(500, update_gt_window)
    
    update_gt_window()

def update_orbit_plot():
    with orbit_lock:
        if len(orbit_x) > 0:
            orbit_line.set_data(orbit_x, orbit_y)
            orbit_line.set_3d_properties(orbit_z)
            
            orbit_point._offsets3d = ([orbit_x[-1]], [orbit_y[-1]], [orbit_z[-1]])
            
            max_coord = max(
                max(abs(x) for x in orbit_x),
                max(abs(y) for y in orbit_y),
                max(abs(z) for z in orbit_z)
            )
            if max_coord > 6.5e6:
                lim = max_coord * 1.1
                ax_orbit.set_xlim(-lim, lim)
                ax_orbit.set_ylim(-lim, lim)
                ax_orbit.set_zlim(-lim, lim)
    
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
    if local_med == True:
        temps_med.append(calc_med(tempmeds))
    else:    
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

def calc_med(tempmeds):
    return sum(tempmeds) / len(tempmeds)

def camb_med():
    global local_med
    send_command("42:1")
    if local_med == True:
        local_med = False
        messagebox.showinfo("Cambio cálculo media", "La media se calculará en el satélite")
    else:
        local_med = True
        messagebox.showinfo("Cambio cálculo media", "La media se calculara localmente")


title_label = Label(window, text="🛰️ CONTROL SATÉLITE", font=title_font, bg=bg_main, fg=tf)
title_label.pack(pady=15)

graphs_frame = Frame(window, bg=bg_main)
graphs_frame.pack(pady=10, padx=20)

orbit_frame = Frame(graphs_frame, bg="navy")
orbit_frame.grid(row=0, column=0, padx=10)

radar_frame = Frame(graphs_frame, bg="navy")
radar_frame.grid(row=0, column=1, padx=10)

temp_frame = Frame(graphs_frame, bg="navy")
temp_frame.grid(row=0, column=2, padx=10)

controls_frame = Frame(window, bg=bg_main)
controls_frame.pack(pady=15, padx=20)

transmission_frame = Frame(controls_frame, bg=bg_main)
transmission_frame.grid(row=0, column=0, padx=20, sticky=N)

interval_frame = Frame(transmission_frame, bg=bg_main)
interval_frame.pack(pady=(15, 0))

mode_frame = Frame(controls_frame, bg=bg_main)
mode_frame.grid(row=0, column=1, padx=20, sticky=N)

events_frame = Frame(controls_frame, bg=bg_main)
events_frame.grid(row=0, column=2, padx=20, sticky=N)

obs_container = Frame(events_frame, bg=bg_main)
obs_container.pack(pady=5)
obs_label = Label(obs_container, text="Nota:", font=font_std, bg=bg_main, fg=tf)
obs_label.grid(row=0, column=0, padx=(0, 5), sticky=W)
obs_entry = Entry(obs_container, font=("Arial", 10), width=20, fg="gray")
obs_entry.grid(row=0, column=1, padx=0, ipady=3)
obs_entry.insert(0, "Escribe aquí...")
btn_add_obs = Button(obs_container, text="➕", font=("Arial", 12, "bold"), bg="deepskyblue", fg=tf, relief=FLAT, bd=0, padx=8, pady=6)
btn_add_obs.grid(row=0, column=2, padx=(5, 0))

panel_frame = Frame(events_frame, bg="navy")
panel_frame.pack(pady=10)

Label(orbit_frame, text="🌍 Órbita Satelital (3D)", font=subtitle_font, bg=bg_main, fg=tf).pack()
Label(radar_frame, text="📡 Sonar de Distancia", font=subtitle_font, bg=bg_main, fg=tf).pack()
Label(temp_frame, text="🌡️ Temperatura y Humedad", font=subtitle_font, bg=bg_main, fg=tf).pack()
Label(transmission_frame, text="📡 Transmisión", font=subtitle_font, bg=bg_main, fg=tf).pack(pady=5)
Label(interval_frame, text="⏱️ Intervalo (ms):", font=font_std, bg=bg_main, fg=tf).grid(row=0, column=0, columnspan=2, pady=(0, 5))
Label(mode_frame, text="⚙️ Modo Operación", font=subtitle_font, bg=bg_main, fg=tf).pack(pady=5)
Label(events_frame, text="📝 Observaciones", font=subtitle_font, bg=bg_main, fg=tf).pack(pady=5)

btn_iniciar = Button(transmission_frame, text="▶ Iniciar Transmisión", font=button_font, bg=btcl, fg=tf, relief=FLAT, bd=0, padx=20, pady=10, width=20)
btn_iniciar.pack(pady=5)
btn_parar = Button(transmission_frame, text="⏸ Parar Transmisión", font=button_font, bg=btcl, fg=tf, relief=FLAT, bd=0, padx=20, pady=10, width=20)
btn_parar.pack(pady=5)
btn_reanudar = Button(transmission_frame, text="⏯ Reanudar Transmisión", font=button_font, bg=btcl, fg=tf, relief=FLAT, bd=0, padx=20, pady=10, width=20)
btn_reanudar.pack(pady=5)

entry_tiempo = Entry(interval_frame, font=("Arial", 12), width=12)
entry_tiempo.grid(row=1, column=0, padx=(0, 5), ipady=3)
entry_tiempo.insert(0, "200-10000")

btn_validar = Button(interval_frame, text="✓ Enviar", font=("Arial", 10, "bold"), bg="deepskyblue", fg="white", relief=FLAT, bd=0, padx=15, pady=10, command=leer_vel)
btn_validar.grid(row=1, column=1, padx=(5, 0))

btn_auto = Button(mode_frame, text="🔄 Modo Automático", font=button_font, bg=btcl, fg=tf, relief=FLAT, bd=0, padx=20, pady=10, width=20)
btn_auto.pack(pady=10)
btn_manual = Button(mode_frame, text="🎮 Modo Manual", font=button_font, bg=btcl, fg=tf, relief=FLAT, bd=0, padx=20, pady=10, width=20)
btn_manual.pack(pady=10)
btn_media = Button(mode_frame, text="🌡️Sitio cálculo temp media", font=button_font, bg=btcl, fg=tf, relief=FLAT, bd=0, padx=20, pady=10, width=20, command=camb_med )
btn_media.pack(pady=10)

# ✅ NUEVO: Control manual de ángulo
manual_angle_frame = Frame(mode_frame, bg=bg_main)
manual_angle_frame.pack(pady=10)

Label(manual_angle_frame, text="🎯 Ángulo Manual:", font=font_std, bg=bg_main, fg=tf).pack(pady=(0, 5))

angle_input_frame = Frame(manual_angle_frame, bg=bg_main)
angle_input_frame.pack()

entry_angulo = Entry(angle_input_frame, font=("Arial", 12), width=8)
entry_angulo.pack(side=LEFT, padx=(0, 5), ipady=3)
entry_angulo.insert(0, "0-180")

def enviar_angulo_manual():
    angulo_raw = entry_angulo.get()
    try:
        angulo = int(angulo_raw)
        if 0 <= angulo <= 180:
            send_command(f"2:{angulo}")
            messagebox.showinfo("✓ Enviado", f"Ángulo configurado: {angulo}°")
        else:
            messagebox.showerror("Error", f"Valor fuera de rango (0-180): {angulo}")
    except ValueError:
        messagebox.showerror("Error", "Introduce un valor numérico válido")

btn_enviar_angulo = Button(angle_input_frame, text="➤ Enviar", 
                           font=("Arial", 10, "bold"), 
                           bg="deepskyblue", fg="white", 
                           relief=FLAT, bd=0, padx=15, pady=8, 
                           command=enviar_angulo_manual)
btn_enviar_angulo.pack(side=LEFT)

def on_obs_focus_in(event):
    if obs_entry.get() == "Escribe aquí...":
        obs_entry.delete(0, END)
        obs_entry.config(fg="black")

def on_obs_focus_out(event):
    if obs_entry.get() == "":
        obs_entry.insert(0, "Escribe aquí...")
        obs_entry.config(fg="gray")

obs_entry.bind("<FocusIn>", on_obs_focus_in)
obs_entry.bind("<FocusOut>", on_obs_focus_out)
btn_ver_eventos = Button(events_frame, text="📋 Ver Eventos", font=button_font, bg=btcl, fg=tf, relief=FLAT, bd=0, padx=20, pady=8, width=20)
btn_ver_eventos.pack(pady=10)
btn_ground_track = Button(events_frame, text="🗺️ Ver Ground Track", font=button_font, bg=btcl, fg=tf, relief=FLAT, bd=0, padx=20, pady=8, width=20)
btn_ground_track.pack(pady=5)

Label(panel_frame, text="☀️ Estado Panel Solar", font=("Arial", 10, "bold"), bg="navy", fg="white").pack(pady=3)
panel_label = Label(panel_frame, text="RETRAÍDO", font=("Arial", 12, "bold"), bg="#ff6b6b", fg="white", padx=15, pady=5)
panel_label.pack(pady=5, padx=10)

fig_orbit = plt.figure(figsize=(5, 4.5), facecolor='#0a0a2e')
ax_orbit = fig_orbit.add_subplot(111, projection='3d')
ax_orbit.set_facecolor('#0a0a2e')
fig_orbit.patch.set_facecolor('#0a0a2e')

R_EARTH = 6371000
u = np.linspace(0, 2 * np.pi, 30)
v = np.linspace(0, np.pi, 20)
x_earth = R_EARTH * np.outer(np.cos(u), np.sin(v))
y_earth = R_EARTH * np.outer(np.sin(u), np.sin(v))
z_earth = R_EARTH * np.outer(np.ones(np.size(u)), np.cos(v))
ax_orbit.plot_surface(x_earth, y_earth, z_earth, color='green', alpha=0.3, linewidth=0)

orbit_line, = ax_orbit.plot([], [], [], 'cyan', linewidth=1.5, label='Trayectoria')
orbit_point = ax_orbit.scatter([], [], [], color='red', s=80, marker='o', label='Satélite', depthshade=True)

ax_orbit.set_xlim(-7e6, 7e6)
ax_orbit.set_ylim(-7e6, 7e6)
ax_orbit.set_zlim(-7e6, 7e6)
ax_orbit.set_xlabel('X (m)', color='white', fontsize=8)
ax_orbit.set_ylabel('Y (m)', color='white', fontsize=8)
ax_orbit.set_zlabel('Z (m)', color='white', fontsize=8)
ax_orbit.tick_params(colors='white', labelsize=7)
ax_orbit.grid(True, alpha=0.3)
ax_orbit.legend(loc='upper right', fontsize=7)

ax_orbit.xaxis.pane.set_facecolor('#0a0a2e')
ax_orbit.yaxis.pane.set_facecolor('#0a0a2e')
ax_orbit.zaxis.pane.set_facecolor('#0a0a2e')
ax_orbit.xaxis.pane.set_alpha(0.3)
ax_orbit.yaxis.pane.set_alpha(0.3)
ax_orbit.zaxis.pane.set_alpha(0.3)

canvas_orbit = FigureCanvasTkAgg(fig_orbit, master=orbit_frame)
canvas_orbit.get_tk_widget().pack()

fig_radar, ax_radar = plt.subplots(subplot_kw={'polar': True}, figsize=(5, 4.5), facecolor='#0a0a2e')
ax_radar.set_facecolor('#0a0a2e')
max_distance = 500
ax_radar.set_ylim(0, max_distance)
ax_radar.set_thetamin(0)
ax_radar.set_thetamax(180)
ax_radar.set_theta_zero_location('W')
ax_radar.set_theta_direction(-1)
ax_radar.set_xlabel('Distancia (mm)', color='white', fontsize=9)
ax_radar.tick_params(colors='white', labelsize=8)
ax_radar.grid(True, alpha=0.3)
linea_radar, = ax_radar.plot([], [], 'lime', linewidth=2, marker='o', markersize=4)
canvas_radar = FigureCanvasTkAgg(fig_radar, master=radar_frame)
canvas_radar.get_tk_widget().pack()

fig_temp, ax_temp = plt.subplots(figsize=(5, 4.5), facecolor='#0a0a2e')
ax_temp.set_facecolor('#0a0a2e')
ax_temp.set_ylim(0, 100)
ax_temp.set_xlabel('Tiempo (muestras)', color='white', fontsize=9)
ax_temp.set_ylabel('Valor', color='white', fontsize=9)
ax_temp.tick_params(colors='white', labelsize=8)
ax_temp.grid(True, alpha=0.3)
line_temp, = ax_temp.plot(range(max_points), temps, 'red', linewidth=2, label='Temp (°C)')
line_hum, = ax_temp.plot(range(max_points), hums, 'cyan', linewidth=2, label='Hum (%)')
line_med, = ax_temp.plot(range(max_points), temps_med, 'yellow', linewidth=2, label='Temp Media (°C)')
ax_temp.legend(loc='upper right', fontsize=7)
canvas_temp = FigureCanvasTkAgg(fig_temp, master=temp_frame)
canvas_temp.get_tk_widget().pack()

btn_iniciar.config(command=iniClick)
btn_parar.config(command=stopClick)
btn_reanudar.config(command=reanClick)
btn_auto.config(command=os_auto)
btn_manual.config(command=os_manual)
btn_add_obs.config(command=agregar_observacion)
btn_ver_eventos.config(command=abrir_vista_eventos)
btn_ground_track.config(command=abrir_ground_track)

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
