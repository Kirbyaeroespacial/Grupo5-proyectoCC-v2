#Importe de todo lo necesario
import time
import serial
import threading
import time
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
latest_data = {"temp": 0, "hum": 0}
latest_distance = 0  # Añadido: definición de la variable

#Definimos la función read_serial que se encargara de leer los datos:
def read_serial():
        global plot_active
        global latest_distance
        while True:
            linea = usbSerial.readline().decode('utf-8').strip()
            if not linea:
                time.sleep(0.01)
                continue

            # Expected message formats from satellite/GS:
            # 1:hum100:temp100   -> humidity and temperature (values multiplied by 100)
            # 2:distance         -> distance in mm
            # 4:e:1 or 5:e:1    -> error messages
            tokens = linea.split(":")
            try:
                # If the line starts with a numeric ID, parse by ID
                if tokens[0].isdigit():
                    msg_id = int(tokens[0])
                    if msg_id == 1 and len(tokens) >= 3:
                        hum = float(tokens[1]) / 100.0
                        temp = float(tokens[2]) / 100.0
                        latest_data["temp"] = temp
                        latest_data["hum"] = hum
                        print(f"Serial: {temp:.2f} °C, {hum:.2f} %")
                    elif msg_id == 2 and len(tokens) >= 2:
                        try:
                            latest_distance = int(tokens[1])
                            print(f"Distance: {latest_distance} mm")
                        except ValueError:
                            pass
                    elif msg_id in (4, 5):
                        # error messages like "4:e:1" or "5:e:2"
                        plot_active = False
                        if msg_id == 4:
                            messagebox.showerror("Error sensor", "Error en la lectura de los datos del sensor de temperatura y humedad!!!")
                        else:
                            messagebox.showerror("Error sensor", "Error en la lectura de los datos del sensor de distancia!!!")
                    else:
                        # Unknown ID - ignore or log
                        pass
                else:
                    # Legacy/simple lines: try to parse as distance integer
                    try:
                        latest_distance = int(linea)
                        print("correcto")
                    except ValueError:
                        # if contains 'e' short error markers
                        if linea.strip().lower().startswith(("e",)) and len(linea.strip()) <= 3:
                            plot_active = False
                            messagebox.showerror("Error en la transmisión de datos")
                        else:
                            # unknown content - ignore
                            pass
            except Exception:
                # Defensive: ignore malformed serial lines
                pass
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
#Fin creación caja de texto para cambiar la velocidad de transmisión de datos

#Inicio uso de la caja de texto para cambiar la velocidad de transmisión de datos:
def leer_vel ():
    vel_datos_raw = entry.get()
    if vel_datos_raw == placeholder or vel_datos_raw == "":
        messagebox.showerror(
            "Error de datos",
            "No ha introducido ningún valor. Introduzca un valor en ms entre 200 y 10000."
        )
        return
    try:
        vel_datos = int (vel_datos_raw)
        if 200 <= vel_datos <= 10000:
            #NO OLVIDAR DESCOMENTAR AL PROBAR!!!!!!
            usbSerial.write(f"1:{vel_datos}\n".encode())  # Corregido envío por serial
            print("1:",vel_datos)
            messagebox.showinfo("Velodidad introducida correcta", f"Se ha enviado la siguiente velocidad de datos: {vel_datos}")
        else:
            messagebox.showerror("Error de datos", f"Número introducido fuera de rango! {vel_datos}")
    except ValueError:
        messagebox.showerror("Error de datos", f"Se ha introducido un valor no numérico: {vel_datos_raw}")

btn = Button(window, text="Validar", font=("Inter", 14, "bold"),command=leer_vel, bg="#4b6cb7", fg="white",activebackground="#6b8dd6", activeforeground="white",bd=0, relief=RIDGE, padx=20, pady=10)
btn.pack(pady=10)

#Fin velocidad transmisión

#División programa en dos zonas

left_frame = Frame(window, bg= col_izq, width=900, height=600)
left_frame.pack(side=LEFT, fill=BOTH)

right_frame = Frame(window, bg=col_der, width=900, height=600)  # color distinto para distinguir
right_frame.pack(side=RIGHT, fill=BOTH, expand=True)

left_frame.pack_propagate(0)
right_frame.pack_propagate(0)

#Fin división programa dos zonas

#Creación del espacio para los botones (lado izquierdo)
btn_frame_left = Frame(left_frame, bg=col_izq)
btn_frame_left.pack(pady=10)

#Definir crear boton (mas sencillo para la programación consecuente)
def create_btn(master, text, command):
    return Button(
        master, text=text, command=command,
        font=button_font, bg="#4b6cb7", fg="white",
        activebackground="#6b8dd6", activeforeground="white",
        bd=0, relief=RIDGE, padx=20, pady=15, width=18
    )
#Definición de las acciones de los botones
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


#Gráfico dentro de interfaz
fig_plot, ax_plot = plt.subplots(figsize=(7, 4.5))  # Renombrado para evitar conflicto
ax_plot.set_ylim(0, 100)
ax_plot.set_title("Temperatura y Humedad")
line_temp, = ax_plot.plot(range(max_points), temps, label="Temperature")
line_hum, = ax_plot.plot(range(max_points), hums, label="Humidity")
ax_plot.legend()
canvas_plot = FigureCanvasTkAgg(fig_plot, master=left_frame)
canvas_plot.get_tk_widget().pack(pady=20)

#Actualizar gráfica periodicamente
def update_plot():
    temps.append(latest_data["temp"])
    hums.append(latest_data["hum"])
    
    line_temp.set_visible(plot_active)
    line_hum.set_visible(plot_active)
    
    line_temp.set_ydata(temps)
    line_hum.set_ydata(hums)
    line_temp.set_xdata(range(len(temps)))
    line_hum.set_xdata(range(len(hums)))
    
    ax_plot.relim()
    ax_plot.autoscale_view()
    canvas_plot.draw()
    
    window.after(100, update_plot)
#Fin parte izquierda del programa

#Inicio parte derecha del programa
btn_frame_right = Frame(right_frame, bg=col_der)
btn_frame_right.pack(pady=10)

#Definición de las acciones de los botones en la parte derecha
def os_man():
    usbSerial.write(b"4:m\n")
    print("4:m")
def os_auto():
    usbSerial.write(b"4:a\n")
    print("4:a")

create_btn(btn_frame_right, "OS Auto", os_auto).grid(row=0, column=0, padx=10)
create_btn(btn_frame_right, "OS Manual", os_man).grid(row=0, column=1, padx=10)

#Gráfica de radar
max_distance = 1000
categorias = ["Dist"]
N = len(categorias)
angles = np.linspace(0, np.pi, N, endpoint=False).tolist()
angles += angles[:1]

fig_radar, ax_radar = plt.subplots(figsize=(7,4.5), subplot_kw=dict(polar=True))  # Renombrado
line_radar, = ax_radar.plot([], [], color='blue', linewidth=2)
ax_radar.fill([], [], color='blue', alpha=0.25)
ax_radar.set_xticks(angles[:-1])
ax_radar.set_xticklabels(categorias)
ax_radar.set_ylim(0, 100)
ax_radar.set_thetamin(0)
ax_radar.set_thetamax(180)
ax_radar.set_theta_zero_location('W')
ax_radar.set_theta_direction(-1)
ax_radar.set_title("Radar de Distancia", size=16, color=col_der, y=1.05)
ax_radar.tick_params(colors="0000")

canvas_radar = FigureCanvasTkAgg(fig_radar, master=right_frame)
canvas_radar.get_tk_widget().pack(expand=True)

def update_radar():
    valor = min(latest_distance / max_distance * 100, 100)
    values = [valor] + [valor]
    line_radar.set_data(angles, values)

    for coll in ax_radar.collections:
        coll.remove()
    ax_radar.fill(angles, values, color='blue', alpha=0.25)

    canvas_radar.get_tk_widget().after(500, update_radar)
#Fin gráfica de radar

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
