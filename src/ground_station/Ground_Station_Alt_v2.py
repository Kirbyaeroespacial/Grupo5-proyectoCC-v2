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
#device = 'COM7'
#usbSerial = serial.Serial(device, 9600, timeout=1)
#NO OLVIDAR DESCOMENTAR AL PROBAR!!!!

#Búfer de datos
max_points = 100
temps = deque([0]*max_points, maxlen=max_points)
hums = deque([0]*max_points, maxlen=max_points)
latest_data = {"temp": 0, "hum": 0}



#Definimos la función read_serial que se encargara de leer los datos:
def read_serial():
        global plot_active
        global latest_distance
        while True:
            linea = usbSerial.readline().decode('utf-8').strip()
            if ":" in linea:
                ht = linea.split(":")
                try:
                    temp = float(ht[1]) / 100
                    hum = float(ht[0]) / 100
                    latest_data["temp"] = temp
                    latest_data["hum"] = hum
                    print(f"Serial: {temp:.2f} °C, {hum:.2f} %")
                    if "e" in linea:
                        if "4" in linea:
                            messagebox.showerror("Error sensor", f"Error en la lectura de los datos del sensor de temperatura y humedad!!!")
                        elif "5" in linea:
                            messagebox.showerror("Error sensor", f"Error en la lectura de los daros del sensor de distancia!!!")
                except (ValueError, IndexError):
                    # Ignora linees invalidas.
                    pass
            elif "e" in linea:
                plot_active = False
                messagebox.showerror("Error en la transmisión de datos")
            else:
                latest_distance = int(linea)
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
            #NO OLVIDAR DESCIOMENTAR AL PROBAR!!!!!!
            #usbSerial.write(b"1:",vel_datos, "\n") 
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
fig, ax = plt.subplots(figsize=(7, 4.5))
ax.set_ylim(0, 100)
ax.set_title("Temperatura y Humedad")
line_temp, = ax.plot(range(max_points), temps, label="Temperature")
line_hum, = ax.plot(range(max_points), hums, label="Humidity")
ax.legend()
canvas = FigureCanvasTkAgg(fig, master=left_frame)
canvas.get_tk_widget().pack(pady=20)

#Actualizar gráfica periodicamente
def update_plot():
    # Actualizar datos siempre
    temps.append(latest_data["temp"])
    hums.append(latest_data["hum"])
    
    # Mostrar u ocultar líneas
    line_temp.set_visible(plot_active)
    line_hum.set_visible(plot_active)
    
    # Actualizar datos de las líneas
    line_temp.set_ydata(temps)
    line_hum.set_ydata(hums)
    line_temp.set_xdata(range(len(temps)))
    line_hum.set_xdata(range(len(hums)))
    
    ax.relim()
    ax.autoscale_view()
    canvas.draw()
    
    # Llamar de nuevo después de 100 ms
    window.after(100, update_plot)
#Fin parte izquierda del programa

#Inicio parte derecha del programa
#Creación del espacio para los botones (lado izquierdo)
btn_frame_right = Frame(right_frame, bg=col_der)
btn_frame_right.pack(pady=10)

create_btn(btn_frame_right, "OS Auto", iniClick).grid(row=0, column=0, padx=10) #Orientación del Sensor auto
create_btn(btn_frame_right, "OS Manual", stopClick).grid(row=0, column=1, padx=10) #Orientación del Sensor manual

#Definición de las acciones de los botones en la parte derecha
def os_man():
    usbSerial.write(b"4:m\n")
def os_auto():
    usbSerial.write(b"4:a\n")

#Gráfica de radar
#Setup
max_distance = 1000

categorias = ["Dist"]
N = len(categorias)
angles = np.linspace(0, np.pi, N, endpoint=False).tolist()
angles += angles[:1]

#Estilo
fig, ax = plt.subplots(figsize=(7,4.5), subplot_kw=dict(polar=True))
line, = ax.plot([], [], color='blue', linewidth=2)
ax.fill([], [], color='blue', alpha=0.25)
ax.set_xticks(angles[:-1])
ax.set_xticklabels(categorias)
ax.set_ylim(0, 100)

# --- LÍNEAS NUEVAS PARA SEMICIRCULO ARRIBA ---
ax.set_thetamin(0)
ax.set_thetamax(180)
ax.set_theta_zero_location('W')  # 0° arriba
ax.set_theta_direction(-1)       # sentido horario

ax.set_title("Radar de Distancia", size=16, color=col_der, y=1.05)
ax.tick_params(colors="0000")
#Fin setup

canvas = FigureCanvasTkAgg(fig, master=right_frame)
canvas.get_tk_widget().pack(expand=True)

def update_radar():
    valor = min(latest_distance / max_distance * 100, 100)  # normalizar
    values = [valor] + [valor]  # cerrar
    line.set_data(angles, values)

    # Limpiar relleno anterior y dibujar nuevo
    for coll in ax.collections:
        coll.remove()
    ax.fill(angles, values, color='blue', alpha=0.25)

    canvas.get_tk_widget().after(500, update_radar)  # actualización
#Fin gráfica de radar



#Fin parte derecha del programa

window.after(100, update_plot)
def on_close():
    try:
        usbSerial.close()
    except:
        pass
    window.destroy()
    exit()

window.protocol("WM_DELETE_WINDOW", on_close)
window.mainloop()
window.mainloop()#Ejecuta interfaz
