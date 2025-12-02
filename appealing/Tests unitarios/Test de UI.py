#Este código se ha creado con la finalidad de realiza pruebas añadiendo elementos en la interfaz sin necesidad de tener los arduinos conectados.

#Importe de todo lo necesario
import time
import threading
import tkinter as tk
from collections import deque
from tkinter import *
from tkinter import font
from tkinter import messagebox 
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import numpy as np
import matplotlib
import os
import sys
import re

ttt = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed vitae maximus ligula. Morbi consectetur sem vitae ex mollis dapibus. In hac habitasse platea dictumst. Pellentesque habitant morbi tristique senectus et netus et malesuada fames ac turpis egestas. Phasellus rhoncus suscipit ex ut tincidunt. Lorem ipsum dolor sit amet, consectetur adipiscing elit. Sed tincidunt ipsum ac laoreet pharetra. Vivamus lacinia ac quam at pulvinar. Nunc eu semper enim."

matplotlib.use("TkAgg")



plot_active = True

#Cosas del posicionamiento
# === DATOS ORBITALES ===
orbit_x = []
orbit_y = []
orbit_lock = threading.Lock()

# Regex para parsear posición orbital
regex_orbit = re.compile(r"Position: \(X: ([\d\.-]+) m, Y: ([\d\.-]+) m, Z: ([\d\.-]+) m\)")
#PARA LA PRUEBA, BORRAR EN VERSIÓN REAL:
import math
import time

# --- Constants (from Arduino code) ---
G = 6.67430e-11           # Gravitational constant (m^3 kg^-1 s^-2)
M = 5.97219e24            # Mass of Earth (kg)
R_EARTH = 6371000         # Radius of Earth (meters)
ALTITUDE = 400000          # Satellite altitude (meters)
EARTH_ROTATION_RATE = 7.2921159e-5  # rad/s
MILLIS_BETWEEN_UPDATES = 1000       # ms
TIME_COMPRESSION = 90.0

# --- Variables ---
r = R_EARTH + ALTITUDE
real_orbital_period = 2 * math.pi * math.sqrt(r**3 / (G * M))

# Buffers for testing
orbit_x = []
orbit_y = []

# --- Run simulated orbit in background thread ---
def start_orbit_simulation():
    def run():
        global orbit_x, orbit_y
        start_time = time.time()
        while True:  # Infinite loop for continuous testing
            t = (time.time() - start_time) * TIME_COMPRESSION
            angle = 2 * math.pi * (t / real_orbital_period)
            x = r * math.cos(angle)
            y = r * math.sin(angle)
            # Update orbit safely
            with orbit_lock:
                orbit_x.append(x)
                orbit_y.append(y)
                if len(orbit_x) > 500:  # Limit buffer size
                    orbit_x.pop(0)
                    orbit_y.pop(0)
            time.sleep(0.1)  # Update every 100ms

    threading.Thread(target=run, daemon=True).start()

# Start the simulation before starting mainloop
start_orbit_simulation()

#PRUEBA HASTA AQUÍ


#Fin cosas posicionamiento



#Búfer de datos para el sensor de distancia
max_points = 100
temps = deque([0]*max_points, maxlen=max_points)
hums = deque([0]*max_points, maxlen=max_points)
temps_med = deque([0]*max_points, maxlen=max_points)
latest_data = {"temp": 20, "hum": 50}
latest_distance = 200
angulo = 90
latest_temp_med = 21

# listas para trail (ángulos en radianes, radios en mm)
thetas = []
radios = []

#Inicio GUI tinker
window = Tk()
main_col = "#1e1e2f"
sizex = 1910
sizey = 960
sizex_sec = 1280
sizey_sec = 720
window_size = (f"{sizex}x{sizey}")
window.title("Control Satélite (SIN COM)")
window.geometry(window_size)
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

#Título:
frame_top_1 = Frame(window, bg=main_col)
frame_top_1.pack(fill=X, pady=10, padx=10)
Title = Label(frame_top_1, text="Control Satélite", font=title_font, bg="#1e1e2f", fg="#ffffff")
Title.place(relx = 0.5, anchor = 'n')

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

    # Antes se lo enviábamos al puerto serie; ahora solo informamos
    try:
        vel_datos = int(vel_datos_raw)
        if 200 <= vel_datos <= 10000:
            messagebox.showinfo("Velocidad correcta", f"Velocidad seteada: {vel_datos} (modo sin COM)")
        else:
            messagebox.showerror("Error de datos", f"Número fuera de rango! {vel_datos}")
    except ValueError:
        messagebox.showerror("Error de datos", f"Valor no numérico: {vel_datos_raw}")

#Definición nueva ventana
class vent_orbit:
    def __init__(self, parent):
        sec_windowsize = f"{sizex_sec}x{sizey_sec}"
        self.window = tk.Toplevel(parent, bg=main_col)
        self.window.title("Localizando...")
        self.window.geometry(sec_windowsize)
        self.window.resizable(False, False)  # Fixed window size

        # Title
        tk.Label(
            self.window,
            text="Localización del Satélite",
            font=title_font,
            bg="#1e1e2f",
            fg="#ffffff"
        ).pack(pady=20)

        # Plot
        self.fig, self.ax = plt.subplots(figsize=(10, 6))
        self.orbit_line, = self.ax.plot([], [], 'bo-', markersize=2, label='Órbita')
        self.last_point = self.ax.scatter([], [], color='red', s=50, label='Posición actual')

        # Earth circle
        R_EARTH = 6371000
        earth = plt.Circle((0, 0), R_EARTH, color='orange', fill=False, linewidth=2, label='Tierra')
        self.ax.add_artist(earth)

        # Axes
        self.ax.set_xlim(-7e6, 7e6)
        self.ax.set_ylim(-7e6, 7e6)
        self.ax.set_aspect('equal', 'box')
        self.ax.set_xlabel('X (metros)')
        self.ax.set_ylabel('Y (metros)')
        self.ax.grid(True)
        self.ax.legend()

        # Canvas (fills window properly)
        self.canvas = FigureCanvasTkAgg(self.fig, master=self.window)
        self.canvas.get_tk_widget().pack(expand=True, fill="both")  # Changed expand and fill

        # State and update
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

    
    


# Frame para botones superiores
frame_top_2 = Frame(window, bg=main_col)
frame_top_2.pack(fill=X, pady=10, padx=10, ipady=30)
# Frame en la izquierda para quitar el bototn "Abrir..." del medio
create_btn(frame_top_1, "Localizar Satélite", lambda: vent_orbit(window)).pack(side=LEFT, padx=5)
# Frame central para toda la movida de la velocidad
btn_center = create_btn(frame_top_2, "Enviar Velocidad", leer_vel)
btn_center.place(relx=0.5, anchor='n')



#División programa en dos zonas
left_frame = Frame(window, bg=col_izq, width = sizex/2)
left_frame.pack(side=LEFT, fill=BOTH)

right_frame = Frame(window, bg=col_der, width = sizex/2)
right_frame.pack(side=RIGHT, fill=BOTH, expand=True)

left_frame.pack_propagate(0)
right_frame.pack_propagate(0)

#Botones izquierda
btn_frame_left = Frame(left_frame, bg=col_izq)
btn_frame_left.pack(pady=10)

def iniClick():
    global plot_active
    plot_active = True
    messagebox.showinfo("SIMULADO", "Iniciar transmisión (sin COM).")

def stopClick():
    global plot_active
    plot_active = False
    messagebox.showinfo("SIMULADO", "Detenido.")

def reanClick():
    global plot_active
    plot_active = True
    messagebox.showinfo("SIMULADO", "Reanudado.")

create_btn(btn_frame_left, "Iniciar transmisión", iniClick).grid(row=0, column=0, padx=10)
create_btn(btn_frame_left, "Parar transmisión", stopClick).grid(row=0, column=1, padx=10)
create_btn(btn_frame_left, "Reanudar", reanClick).grid(row=0, column=2, padx=10)



#Gráfico izquierda
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
    # Generamos valores simulados
    if plot_active:
        latest_data["temp"] += np.random.uniform(-0.3, 0.3)
        latest_data["hum"] += np.random.uniform(-0.5, 0.5)

    temps.append(latest_data["temp"])
    hums.append(latest_data["hum"])
    temps_med.append(np.mean(temps))

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
    messagebox.showinfo("SIMULADO", "OS Manual (sin COM).")

def os_auto():
    messagebox.showinfo("SIMULADO", "OS Automático (sin COM).")

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

    angulo += np.random.uniform(-2, 2)
    angulo = max(0, min(180, angulo))
    latest_distance += np.random.uniform(-10, 10)
    latest_distance = max(0, min(max_distance, latest_distance))

    theta_now = np.deg2rad(angulo)
    r_now = latest_distance

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
    window.destroy()
    exit()

window.protocol("WM_DELETE_WINDOW", on_close)
window.mainloop()
