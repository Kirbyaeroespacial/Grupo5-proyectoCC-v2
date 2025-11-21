import time
import serial
import threading
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
usbSerial = serial.Serial(device, 9600, timeout=1)

# Búfer de datos
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

# Estadísticas de checksum
total_corrupted = 0

def read_serial():
    """Lee datos del puerto serial y actualiza variables globales"""
    global plot_active, latest_distance, angulo, latest_temp_med, total_corrupted

    while True:
        linea = usbSerial.readline().decode('utf-8').strip()
        if not linea:
            time.sleep(0.01)
            continue

        parts = linea.split(':')
        try:
            if len(parts) >= 2 and parts[0] in ('1','2','3','4','5','6','7','8','67','99'):
                idn = parts[0]
                
                if idn == '1':  # Temperatura y humedad
                    if len(parts) >= 3:
                        try:
                            hum = int(parts[1]) / 100.0
                            temp = int(parts[2]) / 100.0
                            latest_data["temp"] = temp
                            latest_data["hum"] = hum
                            print(f"Temp: {temp:.2f}°C, Hum: {hum:.2f}%")
                        except ValueError:
                            pass

                elif idn == '2':  # Distancia
                    try:
                        latest_distance = int(parts[1])
                        print(f"Distancia: {latest_distance} mm")
                    except ValueError:
                        pass

                elif idn == '3':  # Error transmisión
                    plot_active = False
                    messagebox.showerror("Error transmisión", f"Error: {':'.join(parts[1:])}")

                elif idn == '4':  # Error sensor temp/hum
                    messagebox.showerror("Error sensor", "Error en sensor temp/hum")

                elif idn == '5':  # Error sensor distancia
                    messagebox.showerror("Error sensor", "Error en sensor distancia")

                elif idn == '6':  # Ángulo
                    try:
                        angulo = int(parts[1])
                    except ValueError:
                        messagebox.showerror("Error ángulo", "Valor incorrecto")

                elif idn == '7':  # Temperatura media
                    try:
                        latest_temp_med = int(parts[1]) / 100.0
                    except ValueError:
                        pass

                elif idn == '8':  # Alerta temperatura
                    messagebox.showinfo("Alta temperatura!", "¡PELIGRO! Temp media >100°C")

                elif idn == '67':  # Token (info interna, no afecta)
                    pass

                elif idn == '99':  # Estadísticas de checksum
                    try:
                        corrupted = int(parts[1])
                        total_corrupted += corrupted
                        print(f"[CHECKSUM] Mensajes corruptos descartados: {corrupted} | Total: {total_corrupted}")
                    except ValueError:
                        pass

        except Exception as e:
            print("Parse error:", e)

        time.sleep(0.01)

# Inicia hilo de lectura
threading.Thread(target=read_serial, daemon=True).start()

# === GUI Tkinter ===
window = Tk()
window.title("Control Satélite")
window.geometry("1800x800")
window.configure(bg="#1e1e2f")
window.resizable(False, False)

title_font = font.Font(family="Inter", size=22, weight="bold")
button_font = font.Font(family="Inter", size=14, weight="bold")
col_izq = "#1e292f"
col_der = "#31434d"

Label(window, text="Control Satélite", font=title_font, bg="#1e1e2f", fg="#ffffff").pack(pady=(20, 10))

# Entrada de velocidad de transmisión
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
        messagebox.showerror("Error", "Introduzca un valor entre 200 y 10000 ms")
        return
    try:
        vel_datos = int(vel_datos_raw)
        if 200 <= vel_datos <= 10000:
            usbSerial.write(f"1:{vel_datos}\n".encode())
            messagebox.showinfo("OK", f"Velocidad: {vel_datos} ms")
        else:
            messagebox.showerror("Error", f"Fuera de rango: {vel_datos}")
    except ValueError:
        messagebox.showerror("Error", f"Valor no numérico: {vel_datos_raw}")

Button(window, text="Validar", font=("Inter", 14, "bold"), command=leer_vel,
       bg="#4b6cb7", fg="white", bd=0, padx=20, pady=10).pack(pady=10)

# Frames izquierda y derecha
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

# Gráfico temperatura y humedad
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

def os_man():
    usbSerial.write(b"4:m\n")

def os_auto():
    usbSerial.write(b"4:a\n")

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