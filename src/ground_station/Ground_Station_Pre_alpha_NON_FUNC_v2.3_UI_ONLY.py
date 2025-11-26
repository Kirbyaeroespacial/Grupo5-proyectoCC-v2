import tkinter as tk
from tkinter import font, messagebox
from collections import deque
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import numpy as np

# -----------------------------
# Global configuration
# -----------------------------
main_col = "#1e1e2f"
col_izq = "#1e292f"
col_der = "#31434d"
sizex, sizey = 1200, 700
sizex_sec, sizey_sec = 800, 500
max_points = 100

temps = deque([20]*max_points, maxlen=max_points)
hums = deque([50]*max_points, maxlen=max_points)
temps_med = deque([21]*max_points, maxlen=max_points)
latest_data = {"temp": 20, "hum": 50}
latest_temp_med = 21
latest_distance = 200
angulo = 90
thetas = []
radios = []

title_font = ("Inter", 24, "bold")
button_font = ("Inter", 14, "bold")
plot_active = True

# -----------------------------
# Helper function to create buttons
# -----------------------------
def create_btn(master, text, command):
    return tk.Button(master, text=text, command=command,
                     font=button_font, bg="#4b6cb7", fg="white",
                     activebackground="#6b8dd6", activeforeground="white",
                     bd=0, relief="ridge", padx=20, pady=15, width=18)

# -----------------------------
# Main window
# -----------------------------
window = tk.Tk()
window.title("Control Satélite (SIN COM)")
window.geometry(f"{sizex}x{sizey}")
window.configure(bg=main_col)
window.resizable(False, False)

# -----------------------------
# Top frame: Title + buttons
# -----------------------------
frame_top = tk.Frame(window, bg=main_col)
frame_top.pack(fill="x", pady=10, padx=10)

# Title (centered)
title_label = tk.Label(frame_top, text="Control Satélite", font=title_font,
                       bg=main_col, fg="white")
title_label.pack(side="top", pady=5)

# Buttons frame inside top frame
btn_frame_top = tk.Frame(frame_top, bg=main_col)
btn_frame_top.pack(fill="x", pady=5)

# Left button: Open satellite window
def gtk_gtq():
    top_win = tk.Toplevel(window)
    top_win.title("Localización Satélite")
    top_win.geometry(f"{sizex_sec}x{sizey_sec}")
    top_win.configure(bg=main_col)

    # Title
    tk.Label(top_win, text="Localización del Satélite", font=title_font,
             bg=main_col, fg="white").pack(pady=10)

    # Left/Right frames
    left_frame_sec = tk.Frame(top_win, bg=col_izq)
    left_frame_sec.pack(side="left", fill="both", expand=True)
    right_frame_sec = tk.Frame(top_win, bg=col_der)
    right_frame_sec.pack(side="right", fill="both", expand=True)

    # Left plot: simulated orbit
    fig, ax = plt.subplots(figsize=(4,4))
    orbit_line, = ax.plot([], [], 'bo-', markersize=2)
    last_point, = ax.plot([], [], 'ro', markersize=5)
    ax.set_xlim(-500, 500)
    ax.set_ylim(-500, 500)
    ax.set_title("Satélite Simulado")
    canvas = FigureCanvasTkAgg(fig, master=left_frame_sec)
    canvas.get_tk_widget().pack(expand=True, fill="both")

    x_data, y_data = [], []

    def update_orbit():
        if len(x_data) > 50:
            x_data.pop(0)
            y_data.pop(0)
        new_x = np.random.uniform(-400, 400)
        new_y = np.random.uniform(-400, 400)
        x_data.append(new_x)
        y_data.append(new_y)
        orbit_line.set_data(x_data, y_data)
        last_point.set_data(x_data[-1], y_data[-1])
        canvas.draw()
        top_win.after(200, update_orbit)

    update_orbit()

btn_left = create_btn(btn_frame_top, "Localizar Satélite", gtk_gtq)
btn_left.pack(side="left", padx=5)

# Center button: Enviar velocidad
def leer_vel():
    messagebox.showinfo("SIN COM", "Simulando envío de velocidad")

btn_center = create_btn(btn_frame_top, "Enviar Velocidad", leer_vel)
btn_center.pack(side="left", padx=20)
btn_frame_top.pack_propagate(0)

# -----------------------------
# Main frames (left: plots, right: radar)
# -----------------------------
left_frame = tk.Frame(window, bg=col_izq)
left_frame.pack(side="left", fill="both", expand=True)
right_frame = tk.Frame(window, bg=col_der)
right_frame.pack(side="right", fill="both", expand=True)

# -----------------------------
# Left: Temperature/Humidity plot
# -----------------------------
fig_plot, ax_plot = plt.subplots(figsize=(6,3))
line_temp, = ax_plot.plot(range(max_points), temps, label="Temp")
line_hum, = ax_plot.plot(range(max_points), hums, label="Hum")
line_med, = ax_plot.plot(range(max_points), temps_med, label="Avg Temp")
ax_plot.set_ylim(0,100)
ax_plot.legend()
canvas_plot = FigureCanvasTkAgg(fig_plot, master=left_frame)
canvas_plot.get_tk_widget().pack(expand=True, fill="both", pady=10)

def update_plot():
    if plot_active:
        latest_data["temp"] += np.random.uniform(-0.3,0.3)
        latest_data["hum"] += np.random.uniform(-0.5,0.5)
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

# -----------------------------
# Right: Radar plot
# -----------------------------
fig_rad, ax_rad = plt.subplots(subplot_kw={"polar": True}, figsize=(6,3))
ax_rad.set_ylim(0,500)
line_radar, = ax_rad.plot([], [], 'bo-', linewidth=2, alpha=0.6)
canvas_rad = FigureCanvasTkAgg(fig_rad, master=right_frame)
canvas_rad.get_tk_widget().pack(expand=True, fill="both", pady=10)

def update_radar():
    global latest_distance, angulo
    angulo += np.random.uniform(-2,2)
    angulo = max(0, min(180, angulo))
    latest_distance += np.random.uniform(-10,10)
    latest_distance = max(0, min(500, latest_distance))
    theta_now = np.deg2rad(angulo)
    r_now = latest_distance
    thetas.append(theta_now)
    radios.append(r_now)
    if len(thetas)>20:
        thetas.pop(0)
        radios.pop(0)
    line_radar.set_data(thetas, radios)
    canvas_rad.draw()
    window.after(100, update_radar)

# -----------------------------
# Start updates
# -----------------------------
window.after(100, update_plot)
window.after(500, update_radar)
window.mainloop()
