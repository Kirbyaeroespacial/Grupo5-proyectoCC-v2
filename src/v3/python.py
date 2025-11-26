import time
import serial
import threading
from collections import deque
from tkinter import *
from tkinter import font, messagebox
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import numpy as np

# Puerto serial
usbSerial = serial.Serial('COM7', 9600, timeout=1)

# Datos sensores
max_points = 100
temps = deque([0]*max_points, maxlen=max_points)
hums = deque([0]*max_points, maxlen=max_points)
temps_med = deque([0]*max_points, maxlen=max_points)
latest_data = {"temp":0,"hum":0}
latest_distance = 0
angulo = 90
latest_temp_med = 0
plot_active = True

# Radar
thetas = []
radios = []

# Órbita
orbit_x = []
orbit_y = []
orbit_lock = threading.Lock()

# Corruptos checksum
total_corrupted = 0

# Lectura serial
def read_serial():
    global latest_distance, angulo, latest_temp_med, total_corrupted
    global orbit_x, orbit_y
    while True:
        try:
            linea = usbSerial.readline().decode('utf-8').strip()
            if not linea: 
                time.sleep(0.01)
                continue

            # Posición orbital 9:tiempo:X:Y:Z*
            if linea.startswith('9:'):
                parts = linea.split(':')
                if len(parts)==5:
                    try:
                        x = float(parts[2])
                        y = float(parts[3])
                        with orbit_lock:
                            orbit_x.append(x)
                            orbit_y.append(y)
                        continue
                    except:
                        continue

            # Otras señales
            if '*' in linea: linea = linea.split('*')[0]
            parts = linea.split(':')
            if len(parts)<2: continue
            idn = parts[0]

            if idn=='1' and len(parts)==3:
                latest_data['hum'] = int(parts[1])/100.0
                latest_data['temp'] = int(parts[2])/100.0

            elif idn=='2':
                latest_distance = int(parts[1])

            elif idn=='6':
                angulo = int(parts[1])

            elif idn=='7':
                latest_temp_med = int(parts[1])/100.0

            elif idn=='8':
                messagebox.showwarning("Alerta", "¡Temp media >100°C!")

            elif idn=='99':
                try:
                    total_corrupted += int(parts[1])
                except: pass

        except Exception as e:
            print("Error:", e)
        time.sleep(0.01)

threading.Thread(target=read_serial,daemon=True).start()

# GUI
window = Tk()
window.title("Control Satélite")
window.geometry("1800x800")
window.configure(bg="#1e1e2f")
window.resizable(False,False)

title_font = font.Font(family="Inter", size=22, weight="bold")
button_font = font.Font(family="Inter", size=14, weight="bold")
col_izq = "#1e292f"
col_der = "#31434d"

# Título y órbita
title_frame = Frame(window, bg="#1e1e2f")
title_frame.pack(pady=(20,10))
Label(title_frame, text="Control Satélite", font=title_font, bg="#1e1e2f", fg="white").pack(side=LEFT)

# Botón ver órbita
orbital_window = None
def open_orbital():
    global orbital_window
    if orbital_window is None or not getattr(orbital_window,'active',False):
        orbital_window = Toplevel(window)
        orbital_window.active=True
        orbital_window.title("Órbita Satélite")
        orbital_window.geometry("800x800")
        orbital_window.configure(bg="#1e1e2f")
        fig, ax = plt.subplots(figsize=(7,7))
        orbit_line, = ax.plot([], [], 'bo-', markersize=2, label='Órbita')
        last_point = ax.scatter([], [], color='red', s=50, label='Posición actual')
        R_EARTH = 6371000
        earth = plt.Circle((0,0), R_EARTH, color='orange', fill=False, linewidth=2, label='Tierra')
        ax.add_artist(earth)
        ax.set_xlim(-7e6,7e6); ax.set_ylim(-7e6,7e6)
        ax.set_aspect('equal','box'); ax.set_xlabel('X (m)'); ax.set_ylabel('Y (m)')
        ax.grid(True); ax.legend()
        canvas = FigureCanvasTkAgg(fig, master=orbital_window)
        canvas.get_tk_widget().pack(expand=True, fill=BOTH)

        def update_orbit():
            if not getattr(orbital_window,'active',False): return
            with orbit_lock:
                if len(orbit_x)>0:
                    orbit_line.set_data(orbit_x, orbit_y)
                    last_point.set_offsets([[orbit_x[-1], orbit_y[-1]]])
            canvas.draw()
            orbital_window.after(500, update_orbit)
        update_orbit()

        def close_orbital():
            orbital_window.active=False
            orbital_window.destroy()
        orbital_window.protocol("WM_DELETE_WINDOW", close_orbital)

Button(title_frame, text="🛰️ Ver Órbita", font=("Inter",12,"bold"), command=open_orbital, bg="#6b8dd6", fg="white").pack(side=LEFT)

# Velocidad datos
entry = Entry(window, font=("Inter",14))
entry.pack(pady=20, ipadx=80, ipady=5)
placeholder = "Tiempo entre datos (ms)"
entry.insert(0, placeholder)
def on_entry_click(event):
    if entry.get()==placeholder:
        entry.delete(0,END); entry.config(fg="black")
def on_focus_out(event):
    if entry.get()=="": entry.insert(0,placeholder); entry.config(fg="gray")
entry.bind("<FocusIn>",on_entry_click)
entry.bind("<FocusOut>",on_focus_out)

def enviar_vel():
    val = entry.get()
    if val==placeholder or val=="": messagebox.showerror("Error","Ingrese valor 200-10000")
    else:
        try:
            val_i = int(val)
            if 200<=val_i<=10000: usbSerial.write(f"1:{val_i}\n".encode())
            else: messagebox.showerror("Error","Valor fuera de rango")
        except:
            messagebox.showerror("Error","Valor no numérico")
Button(window,text="Validar",font=("Inter",14,"bold"),command=enviar_vel,bg="#4b6cb7",fg="white").pack(pady=10)

# Frames
left_frame = Frame(window,bg=col_izq,width=900,height=600)
left_frame.pack(side=LEFT,fill=BOTH)
right_frame = Frame(window,bg=col_der,width=900,height=600)
right_frame.pack(side=RIGHT,fill=BOTH,expand=True)
left_frame.pack_propagate(0); right_frame.pack_propagate(0)

# Botones izquierda
btn_frame_left = Frame(left_frame,bg=col_izq)
btn_frame_left.pack(pady=10)
def create_btn(master,text,cmd):
    return Button(master,text=text,command=cmd,font=button_font,bg="#4b6cb7",fg="white",bd=0,padx=20,pady=15,width=18)
def ini(): usbSerial.write(b"3:i\n")
def stop(): usbSerial.write(b"3:p\n")
def rean(): usbSerial.write(b"3:r\n")
create_btn(btn_frame_left,"Iniciar transmisión",ini).grid(row=0,column=0,padx=10)
create_btn(btn_frame_left,"Parar transmisión",stop).grid(row=0,column=1,padx=10)
create_btn(btn_frame_left,"Reanudar",rean).grid(row=0,column=2,padx=10)

# Gráfico temp/hum
fig_plot, ax_plot = plt.subplots(figsize=(7,4.5))
ax_plot.set_ylim(0,100); ax_plot.set_title("Temperatura y Humedad")
line_temp, = ax_plot.plot(range(max_points), temps, label="Temp")
line_hum, = ax_plot.plot(range(max_points), hums, label="Hum")
line_med, = ax_plot.plot(range(max_points), temps_med, label="Temp media")
ax_plot.legend()
canvas_plot = FigureCanvasTkAgg(fig_plot, master=left_frame)
canvas_plot.get_tk_widget().pack(pady=20)
def update_plot():
    temps.append(latest_data["temp"])
    hums.append(latest_data["hum"])
    temps_med.append(latest_temp_med)
    line_temp.set_ydata(temps); line_hum.set_ydata(hums); line_med.set_ydata(temps_med)
    canvas_plot.draw()
    window.after(100, update_plot)
update_plot()

# Botones derecha (modo servo)
btn_frame_right = Frame(right_frame,bg=col_der)
btn_frame_right.pack(pady=10)
def os_auto(): usbSerial.write(b"4:a\n")
def os_man(): usbSerial.write(b"4:m\n")
create_btn(btn_frame_right,"OS Auto",os_auto).grid(row=0,column=0,padx=10)
create_btn(btn_frame_right,"OS Manual",os_man).grid(row=0,column=1,padx=10)

# Radar
fig_r, ax_r = plt.subplots(subplot_kw={'polar':True},figsize=(7,4.5))
ax_r.set_ylim(0,500)
ax_r.set_thetamin(0); ax_r.set_thetamax(180)
ax_r.set_theta_zero_location('W'); ax_r.set_theta_direction(-1)
linea_radar, = ax_r.plot([],[],'bo-',linewidth=2,alpha=0.6)
canvas_radar = FigureCanvasTkAgg(fig_r, master=right_frame)
canvas_radar.get_tk_widget().pack(expand=True)
def update_radar():
    global thetas, radios
    theta_now = np.deg2rad(angulo)
    r_now = min(max(latest_distance,0),500)
    thetas.append(theta_now); radios.append(r_now)
    if len(thetas)>20: thetas.pop(0); radios.pop(0)
    linea_radar.set_data(thetas,radios)
    canvas_radar.draw()
    window.after(100,update_radar)
update_radar()

# Cerrar ventana
def on_close():
    try: usbSerial.close()
    except: pass
    window.destroy()
window.protocol("WM_DELETE_WINDOW", on_close)
window.mainloop()
