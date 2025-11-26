# Import everything needed
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
import re

matplotlib.use("TkAgg")

plot_active = True

# Setup del serial
device = 'COM7'
usbSerial = serial.Serial(device, 9600, timeout=1)
# NO OLVIDAR DESCOMENTAR AL PROBAR!!!!

# Buffers de datos
max_points = 100
temps = deque([0]*max_points, maxlen=max_points)
hums = deque([0]*max_points, maxlen=max_points)
temps_med = deque([0]*max_points, maxlen=max_points)  # temperatura media
latest_data = {"temp": 0, "hum": 0}
latest_distance = 0
angulo = 90
latest_temp_med = 0

# Para trail radar
thetas = []
radios = []

# Datos orbitales
orbit_x = []
orbit_y = []
orbit_lock = threading.Lock()
regex_orbit = re.compile(r"Position: \(X: ([\d\.-]+) m, Y: ([\d\.-]+) m, Z: ([\d\.-]+) m\)")

# --- Serial reading thread ---
def read_serial():
    global plot_active, latest_distance, angulo, latest_temp_med

    while True:
        linea = usbSerial.readline().decode('utf-8').strip()
        if not linea:
            time.sleep(0.01)
            continue

        parts = linea.split(':')
        try:
            if len(parts) >= 2 and parts[0] in ('1','2','3','4','5','6','7','8','9'):
                idn = parts[0]
                if idn == '1' and len(parts) >= 3:
                    try:
                        hum = int(parts[1]) / 100.0
                        temp = int(parts[2]) / 100.0
                        latest_data["temp"] = temp
                        latest_data["hum"] = hum
                    except ValueError:
                        pass

                elif idn == '2':
                    try:
                        latest_distance = int(parts[1])
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
                    try:
                        latest_temp_med = int(parts[1]) / 100.0
                    except ValueError:
                        pass

                elif idn == '8':
                    messagebox.showinfo("Alta temperatura!", "¡PELIGRO! ¡¡La temperatura media excede los 100ºC!!")

                elif idn == '9':  # NUEVO: Posición orbital
                    try:
                        match = regex_orbit.match(':'.join(parts[1:]))
                        if match:
                            x, y, z = map(float, match.groups())
                            with orbit_lock:
                                orbit_x.append(x)
                                orbit_y.append(y)
                                if len(orbit_x) > 500:
                                    orbit_x.pop(0)
                                    orbit_y.pop(0)
                    except Exception as e:
                        print("Error parsing orbital position:", e)

            else:  # Compatibilidad antigua
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

# --- Tkinter GUI ---
window = Tk()
main_col = "#1e1e2f"
sizex = 1910
sizey = 960
sizex_sec = 1280
sizey_sec = 720
window.geometry(f"{sizex}x{sizey}")
window.title("Control Satélite")
window.configure(bg=main_col)
window.resizable(False, False)

title_font = font.Font(family="Inter", size=32, weight="bold")
button_font = font.Font(family="Inter", size=14, weight="bold")
col_izq = "#1e292f"
col_der = "#31434d"

def create_btn(master, text, command):
    return Button(master, text=text, command=command,
                  font=button_font, bg="#4b6cb7", fg="white",
                  activebackground="#4b6dd6", activeforeground="white",
                  bd=0, relief=RIDGE, padx=20, pady=15, width=18)

# --- Top frame ---
frame_top_1 = Frame(window, bg=main_col)
frame_top_1.pack(fill=X, pady=10, padx=10)
Title = Label(frame_top_1, text="Control Satélite", font=title_font, bg=main_col, fg="#ffffff")
Title.place(relx=0.5, anchor='n')

# Velocidad de transmisión
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
            messagebox.showinfo("Velocidad correcta", f"Velocidad de datos enviada: {vel_datos}")
        else:
            messagebox.showerror("Error de datos", f"Número fuera de rango! {vel_datos}")
    except ValueError:
        messagebox.showerror("Error de datos", f"Valor no numérico: {vel_datos_raw}")

# Top buttons
frame_top_2 = Frame(window, bg=main_col)
frame_top_2.pack(fill=X, pady=10, padx=10, ipady=30)
create_btn(frame_top_1, "Localizar Satélite", lambda: vent_orbit(window)).pack(side=LEFT, padx=5)
btn_center = create_btn(frame_top_2, "Enviar Velocidad", leer_vel)
btn_center.place(relx=0.5, anchor='n')

# --- Main frames ---
left_frame = Frame(window, bg=col_izq, width=sizex/2)
left_frame.pack(side=LEFT, fill=BOTH)
right_frame = Frame(window, bg=col_der, width=sizex/2)
right_frame.pack(side=RIGHT, fill=BOTH, expand=True)
left_frame.pack_propagate(0)
right_frame.pack_propagate(0)

# --- Left buttons ---
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

# --- Left plot ---
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

# --- Right buttons ---
btn_frame_right = Frame(right_frame, bg=col_der)
btn_frame_right.pack(pady=10)

def os_man():
    usbSerial.write(b"4:m\n")

def os_auto():
    usbSerial.write(b"4:a\n")

create_btn(btn_frame_right, "OS Auto", os_auto).grid(row=0, column=0, padx=10)
create_btn(btn_frame_right, "OS Manual", os_man).grid(row=0, column=1, padx=10)

# --- Radar plot ---
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

# --- Orbital window ---
class vent_orbit:
    def __init__(self, parent):
        self.window = Toplevel(parent, bg=main_col)
        self.window.title("Localizando...")
        self.window.geometry(f"{sizex_sec}x{sizey_sec}")
        self.window.resizable(False, False)

        Label(self.window, text="Localización del Satélite", font=title_font,
              bg=main_col, fg="#ffffff").pack(pady=20)

        self.fig, self.ax = plt.subplots(figsize=(10,6))
        self.orbit_line, = self.ax.plot([], [], 'bo-', markersize=2, label='Órbita')
        self.last_point = self.ax.scatter([], [], color='red', s=50, label='Posición actual')

        earth = plt.Circle((0,0), 6371000, color='orange', fill=False, linewidth=2, label='Tierra')
        self.ax.add_artist(earth)

        self.ax.set_xlim(-7e6,7e6)
        self.ax.set_ylim(-7e6,7e6)
        self.ax.set_aspect('equal', 'box')
        self.ax.set_xlabel('X (metros)')
        self.ax.set_ylabel('Y (metros)')
        self.ax.grid(True)
        self.ax.legend()

        self.canvas = FigureCanvasTkAgg(self.fig, master=self.window)
        self.canvas.get_tk_widget().pack(expand=True, fill='both')

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

# --- Start updates ---
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
