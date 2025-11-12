#Importe de todo lo necesario
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

#Setup del serial
device = 'COM7'
usbSerial = serial.Serial(device, 9600, timeout=1)
#NO OLVIDAR DESCOMENTAR AL PROBAR!!!!

#Búfer de datos
max_points = 100
temps = deque([0]*max_points, maxlen=max_points)
hums = deque([0]*max_points, maxlen=max_points)
temps_med = deque([0]*max_points, maxlen=max_points)  # <-- temperatura media
latest_data = {"temp": 0, "hum": 0}
latest_distance = 0  # en mm
angulo = 90          # inicializamos ángulo por defecto (grados)
latest_temp_med = 0  # <-- valor actual de temperatura media

# listas para trail (ángulos en radianes, radios en mm)
thetas = []
radios = []

#Definimos la función read_serial que se encargará de leer los datos:
def read_serial():
    global plot_active, latest_distance, angulo, latest_temp_med

    while True:
        linea = usbSerial.readline().decode('utf-8').strip()
        if not linea:
            time.sleep(0.01)
            continue

        parts = linea.split(':')
        try:
            if len(parts) >= 2 and parts[0] in ('1','2','3','4','5','6','7','8'):
                idn = parts[0]
                if idn == '1':
                    # 1:humedad:temperatura
                    if len(parts) >= 3:
                        try:
                            hum = int(parts[1]) / 100.0
                            temp = int(parts[2]) / 100.0
                            latest_data["temp"] = temp
                            latest_data["hum"] = hum
                            print(f"Serial: {temp:.2f} °C, {hum:.2f} %")
                        except ValueError:
                            pass

                elif idn == '2':
                    try:
                        latest_distance = int(parts[1])
                        print(f"Distancia recibida: {latest_distance} mm")
                    except ValueError:
                        pass

                elif idn == '3':
                    plot_active = False
                    messagebox.showerror("Error transmisión", f"Error en el envío de datos: {':'.join(parts[1:])}")

                elif idn == '4':
                    messagebox.showerror("Error sensor", f"Error en sensor temp/hum: {':'.join(parts[1:])}")

                elif idn == '5':
                    messagebox.showerror("Error sensor", f"Error en sensor distancia: {':'.join(parts[1:])}")

                elif idn == '6':
                    try:
                        angulo = int(parts[1])
                    except ValueError:
                        messagebox.showerror("Error ángulo", "Error al recibir ángulo, valor incorrecto.")

                elif idn == '7':
                    # 7:<temperatura media>
                    try:
                        latest_temp_med = int(parts[1]) / 100.0
                    except ValueError:
                        pass

                elif idn == '8':
                    messagebox.showinfo("Alta temperatura!", "¡PELIGRO! ¡¡La temperatura media excede los 100ºC!!")

                # Compatibilidad antigua
            else:
                if ":" in linea:
                    ht = linea.split(":")
                    try:
                        temp = float(ht[1]) / 100
                        hum = float(ht[0]) / 100
                        latest_data["temp"] = temp
                        latest_data["hum"] = hum
                        print(f"Serial (legacy): {temp:.2f} °C, {hum:.2f} %")
                    except (ValueError, IndexError):
                        pass
                else:
                    try:
                        latest_distance = int(linea)
                        print(latest_distance)
                    except:
                        pass
        except Exception as e:
            print("Parse error:", e)

        time.sleep(0.01)

threading.Thread(target=read_serial, daemon=True).start()

#Inicio GUI tinker
window = Tk()
window.title("Control Satélite")
window.geometry("1800x800")
window.configure(bg="#1e1e2f")
window.resizable(False, False)
title_font = font.Font(family="Inter", size=22, weight="bold")
button_font = font.Font(family="Inter", size=14, weight="bold")
col_izq = "#1e292f"
col_der = "#31434d"

#Título:
Label(window, text="Control Satélite", font=title_font, bg="#1e1e2f", fg="#ffffff").pack(pady=(20, 10))

#Inicio creación caja de texto para cambiar la velocidad de transmisión de datos
color_placeholder = "#aaaaaa"
entry = Entry(window, font=("Inter", 14), fg="#1e1e2f")
entry.pack(pady=20, ipadx=80, ipady=5)
placeholder = "Tiempo entre datos (s)"
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
        messagebox.showerror("Error de datos", "Introduzca un valor en ms entre 200 y 10000.")
        return
    try:
        vel_datos = int(vel_datos_raw)
        if 200 <= vel_datos <= 10000:
            usbSerial.write(f"1:{vel_datos}\n".encode())
            print("1:", vel_datos)
            messagebox.showinfo("Velocidad correcta", f"Velocidad de datos enviada: {vel_datos}")
        else:
            messagebox.showerror("Error de datos", f"Número fuera de rango! {vel_datos}")
    except ValueError:
        messagebox.showerror("Error de datos", f"Valor no numérico: {vel_datos_raw}")

btn = Button(window, text="Validar", font=("Inter", 14, "bold"), command=leer_vel,
             bg="#4b6cb7", fg="white", activebackground="#6b8dd6",
             activeforeground="white", bd=0, relief=RIDGE, padx=20, pady=10)
btn.pack(pady=10)

#División programa en dos zonas
left_frame = Frame(window, bg=col_izq, width=900, height=600)
left_frame.pack(side=LEFT, fill=BOTH)

right_frame = Frame(window, bg=col_der, width=900, height=600)
right_frame.pack(side=RIGHT, fill=BOTH, expand=True)

left_frame.pack_propagate(0)
right_frame.pack_propagate(0)

#Botones izquierda
btn_frame_left = Frame(left_frame, bg=col_izq)
btn_frame_left.pack(pady=10)

def create_btn(master, text, command):
    return Button(master, text=text, command=command,
                  font=button_font, bg="#4b6cb7", fg="white",
                  activebackground="#4b6dd6", activeforeground="white",
                  bd=0, relief=RIDGE, padx=20, pady=15, width=18)

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

#Gráfico izquierda
fig_plot, ax_plot = plt.subplots(figsize=(7, 4.5))
ax_plot.set_ylim(0, 100)
ax_plot.set_title("Temperatura y Humedad")
line_temp, = ax_plot.plot(range(max_points), temps, label="Temperature")
line_hum, = ax_plot.plot(range(max_points), hums, label="Humidity")
line_med, = ax_plot.plot(range(max_points), temps_med, label="Avg. temp")  # corregido
ax_plot.legend()
canvas_plot = FigureCanvasTkAgg(fig_plot, master=left_frame)
canvas_plot.get_tk_widget().pack(pady=20)

def update_plot():
    temps.append(latest_data["temp"])
    hums.append(latest_data["hum"])
    temps_med.append(latest_temp_med)  # corregido

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

#Parte derecha
btn_frame_right = Frame(right_frame, bg=col_der)
btn_frame_right.pack(pady=10)

def os_man():
    usbSerial.write(b"4:m\n")

def os_auto():
    usbSerial.write(b"4:a\n")

create_btn(btn_frame_right, "OS Auto", os_auto).grid(row=0, column=0, padx=10)
create_btn(btn_frame_right, "OS Manual", os_man).grid(row=0, column=1, padx=10)

#Gráfica radar
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
    print(angulo)
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
