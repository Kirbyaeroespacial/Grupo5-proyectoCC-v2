import time
import serial
import threading
from collections import deque
from tkinter import *
from tkinter import font, messagebox
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import numpy as np
import matplotlib
matplotlib.use("TkAgg")

# ---------------------------------------------
# CONFIGURACIÓN SERIAL
# ---------------------------------------------
device = 'COM7'  # Cambiar por el puerto correcto
usbSerial = serial.Serial(device, 9600, timeout=1)

# ---------------------------------------------
# VARIABLES GLOBALES
# ---------------------------------------------
plot_active = True
max_points = 100
temps = deque([0]*max_points, maxlen=max_points)
hums = deque([0]*max_points, maxlen=max_points)
medias = deque([0]*max_points, maxlen=max_points)
latest_data = {"temp": 0, "hum": 0, "media": 0}
latest_distance = 0

# ---------------------------------------------
# FUNCIÓN DE LECTURA SERIAL
# ---------------------------------------------
def read_serial():
    global plot_active, latest_distance
    while True:
        linea = usbSerial.readline().decode('utf-8').strip()
        if not linea:
            time.sleep(0.01)
            continue

        parts = linea.split(':')
        try:
            if len(parts) >= 2 and parts[0] in ('1','2','3','4','5','6','7'):
                idn = parts[0]
                if idn == '1':
                    # 1:humedad:temperatura
                    if len(parts) >= 3:
                        hum = int(parts[1]) / 100.0
                        temp = int(parts[2]) / 100.0
                        latest_data["temp"] = temp
                        latest_data["hum"] = hum
                        print(f"Temp: {temp:.2f} °C, Hum: {hum:.2f} %")

                elif idn == '2':
                    latest_distance = int(parts[1])
                    print(f"Distancia: {latest_distance} mm")

                elif idn == '3':
                    plot_active = False
                    messagebox.showerror("Error transmisión", f"Error en envío: {':'.join(parts[1:])}")

                elif idn == '4':
                    messagebox.showerror("Error sensor", f"Error sensor temp/hum: {':'.join(parts[1:])}")

                elif idn == '5':
                    messagebox.showerror("Error sensor", f"Error sensor distancia: {':'.join(parts[1:])}")

                elif idn == '6':
                    # 6:media_temp
                    media_temp = int(parts[1]) / 100.0
                    latest_data["media"] = media_temp
                    print(f"Media Temp (10 ultimas): {media_temp:.2f} °C")

                elif idn == '7':
                    alerta = ':'.join(parts[1:])
                    print(f"⚠️ ALERTA RECIBIDA: {alerta}")
                    messagebox.showwarning("Alerta Satélite", f"¡{alerta} detectada!")

            else:
                # Compatibilidad antigua
                if ":" in linea:
                    ht = linea.split(":")
                    try:
                        temp = float(ht[1]) / 100
                        hum = float(ht[0]) / 100
                        latest_data["temp"] = temp
                        latest_data["hum"] = hum
                    except (ValueError, IndexError):
                        pass
                else:
                    try:
                        latest_distance = int(linea)
                    except:
                        pass

        except Exception as e:
            print("Parse error:", e)
        time.sleep(0.01)

threading.Thread(target=read_serial, daemon=True).start()

# ---------------------------------------------
# CONFIGURACIÓN GUI
# ---------------------------------------------
window = Tk()
window.title("Control Satélite")
window.geometry("1800x800")
window.configure(bg="#1e1e2f")
window.resizable(False, False)
title_font = font.Font(family="Inter", size=22, weight="bold")
button_font = font.Font(family="Inter", size=14, weight="bold")

Label(window, text="Control Satélite", font=title_font, bg="#1e1e2f", fg="#ffffff").pack(pady=(20, 10))

# ---------------------------------------------
# ENTRADA VELOCIDAD TRANSMISIÓN
# ---------------------------------------------
color_placeholder = "#aaaaaa"
entry = Entry(window, font=("Inter", 14), fg="#1e1e2f")
entry.pack(pady=20, ipadx=80, ipady=5)
placeholder = "Tiempo entre datos (ms)"
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

def leer_vel():
    vel_datos_raw = entry.get()
    if vel_datos_raw == placeholder or vel_datos_raw == "":
        messagebox.showerror("Error", "Introduzca un valor en ms entre 200 y 10000.")
        return
    try:
        vel_datos = int(vel_datos_raw)
        if 200 <= vel_datos <= 10000:
            usbSerial.write(f"1:{vel_datos}\n".encode())
            messagebox.showinfo("OK", f"Velocidad establecida: {vel_datos} ms")
        else:
            messagebox.showerror("Error", "Valor fuera de rango (200-10000)")
    except ValueError:
        messagebox.showerror("Error", "Debe introducir un número válido.")

Button(window, text="Validar", font=button_font, command=leer_vel, bg="#4b6cb7", fg="white").pack(pady=10)

# ---------------------------------------------
# ZONAS DE LA INTERFAZ
# ---------------------------------------------
col_izq = "#1e292f"
col_der = "#31434d"
left_frame = Frame(window, bg=col_izq, width=900, height=600)
left_frame.pack(side=LEFT, fill=BOTH)
right_frame = Frame(window, bg=col_der, width=900, height=600)
right_frame.pack(side=RIGHT, fill=BOTH, expand=True)
left_frame.pack_propagate(0)
right_frame.pack_propagate(0)

# ---------------------------------------------
# BOTONES PRINCIPALES
# ---------------------------------------------
def create_btn(master, text, command):
    return Button(master, text=text, command=command,
                  font=button_font, bg="#4b6cb7", fg="white",
                  activebackground="#6b8dd6", activeforeground="white",
                  bd=0, relief=RIDGE, padx=20, pady=15, width=18)

btn_frame_left = Frame(left_frame, bg=col_izq)
btn_frame_left.pack(pady=10)

def iniClick():
    global plot_active
    usbSerial.write(b"3:i\n")
    plot_active = True

def stopClick():
    global plot_active
    usbSerial.write(b"3:p\n")
    plot_active = False

def reanClick():
    global plot_active
    usbSerial.write(b"3:r\n")
    plot_active = True

create_btn(btn_frame_left, "Iniciar transmisión", iniClick).grid(row=0, column=0, padx=10)
create_btn(btn_frame_left, "Parar transmisión", stopClick).grid(row=0, column=1, padx=10)
create_btn(btn_frame_left, "Reanudar", reanClick).grid(row=0, column=2, padx=10)

# ---------------------------------------------
# GRÁFICA TEMPERATURA / HUMEDAD / MEDIA
# ---------------------------------------------
fig_plot, ax_plot = plt.subplots(figsize=(7, 4.5))
ax_plot.set_ylim(0, 120)
ax_plot.set_title("Temperatura, Humedad y Media 10 muestras")
line_temp, = ax_plot.plot(range(max_points), temps, label="Temperatura")
line_hum, = ax_plot.plot(range(max_points), hums, label="Humedad")
line_media, = ax_plot.plot(range(max_points), medias, label="Media Temp (10)", linestyle="--", color="orange")
ax_plot.legend()
canvas_plot = FigureCanvasTkAgg(fig_plot, master=left_frame)
canvas_plot.get_tk_widget().pack(pady=20)

def update_plot():
    temps.append(latest_data["temp"])
    hums.append(latest_data["hum"])
    medias.append(latest_data["media"])
    line_temp.set_ydata(temps)
    line_hum.set_ydata(hums)
    line_media.set_ydata(medias)
    ax_plot.relim()
    ax_plot.autoscale_view()
    canvas_plot.draw()
    window.after(200, update_plot)

# ---------------------------------------------
# GRÁFICA RADAR DISTANCIA
# ---------------------------------------------
max_distance = 1000
categorias = ["Dist"]
N = len(categorias)
angles = np.linspace(0, np.pi, N, endpoint=False).tolist()
angles += angles[:1]

fig_radar, ax_radar = plt.subplots(figsize=(7, 4.5), subplot_kw=dict(polar=True))
initial_values = [0.0] * len(angles)
line_radar, = ax_radar.plot(angles, initial_values, linewidth=2)
fill_radar = ax_radar.fill(angles, initial_values, alpha=0.25)
ax_radar.set_xticks(angles[:-1])
ax_radar.set_xticklabels(categorias)
ax_radar.set_ylim(0, 100)
ax_radar.set_thetamin(0)
ax_radar.set_thetamax(180)
ax_radar.set_theta_zero_location('W')
ax_radar.set_theta_direction(-1)
ax_radar.set_title("Radar de Distancia", size=16)
canvas_radar = FigureCanvasTkAgg(fig_radar, master=right_frame)
canvas_radar.get_tk_widget().pack(expand=True)

def update_radar():
    global latest_distance
    valor = min(max(latest_distance / max_distance * 100.0, 0.0), 100.0)
    values = [valor] * (len(angles))
    line_radar.set_data(angles, values)
    for coll in list(ax_radar.collections):
        coll.remove()
    ax_radar.fill(angles, values, alpha=0.25)
    canvas_radar.draw()
    window.after(500, update_radar)

# ---------------------------------------------
# CONTROL CIERRE
# ---------------------------------------------
def on_close():
    try:
        usbSerial.close()
    except:
        pass
    window.destroy()
    exit()

window.after(200, update_plot)
window.after(500, update_radar)
window.protocol("WM_DELETE_WINDOW", on_close)
window.mainloop()
