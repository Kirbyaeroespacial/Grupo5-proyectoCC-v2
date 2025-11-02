import serial
import threading
import time
from collections import deque
from tkinter import *
from tkinter import font
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg

plot_active = True

#Setup del serial
device = 'COM7'
usbSerial = serial.Serial(device, 9600, timeout=1)

#Búfer de datos
max_points = 100
temps = deque([0]*max_points, maxlen=max_points)
hums = deque([0]*max_points, maxlen=max_points)
latest_data = {"temp": 0, "hum": 0}



#Definimos la función read_serial que se encargara de leer los datos:
def read_serial():
        global plot_active
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
                except (ValueError, IndexError):
                    # Ignora linees invalides.
                    pass
            elif "e" in linea:
                plot_active = False
            time.sleep(0.01)


threading.Thread(target=read_serial, daemon=True).start()

#Inicio GUI tinker
window = Tk()
window.title("Control Satélite")
window.geometry("900x600")
window.configure(bg="#1e1e2f")
window.resizable(False, False)

title_font = font.Font(family="Inter", size=22, weight="bold")
button_font = font.Font(family="Inter", size=14, weight="bold")

Label(window, text="Control Satélite", font=title_font, bg="#1e1e2f", fg="#ffffff").pack(pady=(20, 10))

btn_frame = Frame(window, bg="#1e1e2f")
btn_frame.pack(pady=10)

def create_btn(master, text, command):
    return Button(
        master, text=text, command=command,
        font=button_font, bg="#4b6cb7", fg="white",
        activebackground="#6b8dd6", activeforeground="white",
        bd=0, relief=RIDGE, padx=20, pady=15, width=18
    )

def iniClick(): 
    global plot_active
    usbSerial.write(b"i\n")
    plot_active = True
def stopClick(): 
    global plot_active
    usbSerial.write(b"p\n")
    plot_active = False
def reanClick(): 
    global plot_active
    usbSerial.write(b"r\n")
    plot_active = True

create_btn(btn_frame, "Iniciar transmisión", iniClick).grid(row=0, column=0, padx=10)
create_btn(btn_frame, "Parar transmisión", stopClick).grid(row=0, column=1, padx=10)
create_btn(btn_frame, "Reanudar", reanClick).grid(row=0, column=2, padx=10)


#Gráfico dentro de interfaz
fig, ax = plt.subplots(figsize=(7, 3))
ax.set_ylim(0, 100)
ax.set_xlabel("Samples")
ax.set_ylabel("Value")
ax.set_title("Temperatura y Humedad")
line_temp, = ax.plot(range(max_points), temps, label="Temperature")
line_hum, = ax.plot(range(max_points), hums, label="Humidity")
ax.legend()
canvas = FigureCanvasTkAgg(fig, master=window)
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
