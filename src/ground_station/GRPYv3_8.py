import time
import serial
import threading
import re
from collections import deque
from tkinter import *
from tkinter import font
from tkinter import messagebox
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import numpy as np
import matplotlib
matplotlib.use("TkAgg")

# ---------------- Serial ----------------
device = 'COM7'  # Cambiar según tu puerto
usbSerial = serial.Serial(device, 9600, timeout=1)

# ---------------- Buffers ----------------
max_points = 100
temps = deque([0]*max_points, maxlen=max_points)
hums = deque([0]*max_points, maxlen=max_points)
temps_med = deque([0]*max_points, maxlen=max_points)

latest_data = {"temp":0, "hum":0}
latest_distance = 0
angulo = 90
latest_temp_med = 0
plot_active = True
total_corrupted = 0

# ---------------- Órbita ----------------
orbit_x = []
orbit_y = []
orbit_lock = threading.Lock()
regex_orbit = re.compile(r"9:\d+:([\d\.-]+):([\d\.-]+):([\d\.-]+)")

# ---------------- Serial Read ----------------
def read_serial():
    global latest_distance, angulo, latest_temp_med, plot_active, total_corrupted
    while True:
        try:
            linea = usbSerial.readline().decode('utf-8').strip()
            if not linea:
                time.sleep(0.01)
                continue

            # Posición orbital
            match = regex_orbit.search(linea)
            if match:
                try:
                    x = float(match.group(1))
                    y = float(match.group(2))
                    with orbit_lock:
                        orbit_x.append(x)
                        orbit_y.append(y)
                    #print(f"Orbital: X={x}, Y={y}")
                except:
                    pass
                continue

            # Parse IDs
            parts = linea.split(':')
            if len(parts)<2:
                continue
            idn = parts[0]

            if idn=='1' and len(parts)>=3:  # Temp/Hum
                try:
                    hum = int(parts[1])/100.0
                    temp = int(parts[2])/100.0
                    latest_data["hum"] = hum
                    latest_data["temp"] = temp
                except:
                    pass
            elif idn=='2':  # Distancia
                try: latest_distance = int(parts[1])
                except: pass
            elif idn=='6':  # Ángulo servo
                try: angulo = int(parts[1])
                except: pass
            elif idn=='7':  # Temp media
                try: latest_temp_med = int(parts[1])/100.0
                except: pass
            elif idn=='8':  # Alerta temperatura
                messagebox.showinfo("Alta temperatura","¡PELIGRO! Temp media >100°C")
            elif idn=='4':
                messagebox.showerror("Error sensor","Error en sensor temp/hum")
            elif idn=='5':
                messagebox.showerror("Error sensor","Error en sensor distancia")
            elif idn=='3':
                plot_active = False
                messagebox.showerror("Transmisión","Error transmisión")
            elif idn=='67':
                pass
            elif idn=='99':
                try: total_corrupted += int(parts[1])
                except: pass

        except Exception as e:
            print("Parse error:", e)
        time.sleep(0.01)

threading.Thread(target=read_serial,daemon=True).start()

# ---------------- Ventana Órbita ----------------
class VentanaOrbital:
    def __init__(self,parent):
        self.window = Toplevel(parent)
        self.window.title("Órbita Satelital")
        self.window.geometry("800x800")
        self.window.configure(bg="#1e1e2f")
        Label(self.window,text="Vista Orbital (Plano Ecuatorial)",bg="#1e1e2f",fg="white",
              font=("Inter",16,"bold")).pack(pady=10)

        self.fig,self.ax = plt.subplots(figsize=(7,7))
        self.orbit_line, = self.ax.plot([],[],'bo-',markersize=2,label='Órbita')
        self.last_point = self.ax.scatter([],[],color='red',s=50,label='Posición actual')
        R_EARTH = 6371000
        earth = plt.Circle((0,0),R_EARTH,color='orange',fill=False,linewidth=2,label='Tierra')
        self.ax.add_artist(earth)
        self.ax.set_xlim(-7e6,7e6)
        self.ax.set_ylim(-7e6,7e6)
        self.ax.set_aspect('equal','box')
        self.ax.grid(True)
        self.ax.legend()
        self.canvas = FigureCanvasTkAgg(self.fig,master=self.window)
        self.canvas.get_tk_widget().pack(expand=True,fill=BOTH)
        self.active=True
        self.window.protocol("WM_DELETE_WINDOW",self.on_close)
        self.update_plot()

    def update_plot(self):
        if not self.active: return
        with orbit_lock:
            if len(orbit_x)>0:
                self.orbit_line.set_data(orbit_x,orbit_y)
                self.last_point.set_offsets([[orbit_x[-1],orbit_y[-1]]])
        self.canvas.draw()
        self.window.after(500,self.update_plot)

    def on_close(self):
        self.active=False
        self.window.destroy()

# ---------------- GUI ----------------
window = Tk()
window.title("Control Satélite")
window.geometry("1800x800")
window.configure(bg="#1e1e2f")
window.resizable(False,False)

title_font = font.Font(family="Inter",size=22,weight="bold")
button_font = font.Font(family="Inter",size=14,weight="bold")
col_izq = "#1e292f"
col_der = "#31434d"

title_frame = Frame(window,bg="#1e1e2f")
title_frame.pack(pady=(20,10))
Label(title_frame,text="Control Satélite",font=title_font,bg="#1e1e2f",fg="white").pack(side=LEFT,padx=20)

orbital_window = None
def open_orbital():
    global orbital_window
    if orbital_window is None or not orbital_window.active:
        orbital_window = VentanaOrbital(window)

Button(title_frame,text="🛰️ Ver Órbita",font=("Inter",12,"bold"),
       command=open_orbital,bg="#6b8dd6",fg="white",bd=0,padx=15,pady=8).pack(side=LEFT)

# ---------------- Tiempo entre datos ----------------
entry = Entry(window,font=("Inter",14),fg="#1e1e2f")
entry.pack(pady=20,ipadx=80,ipady=5)
placeholder = "Tiempo entre datos (ms)"
entry.insert(0,placeholder)

def on_entry_click(event):
    if entry.get()==placeholder:
        entry.delete(0,END)
        entry.config(fg="black")
def on_focus_out(event):
    if entry.get()=="":
        entry.insert(0,placeholder)
        entry.config(fg="gray")

entry.bind("<FocusIn>",on_entry_click)
entry.bind("<FocusOut>",on_focus_out)

def set_vel():
    val = entry.get()
    if val=="" or val==placeholder:
        messagebox.showerror("Error","Introduzca valor entre 200-10000 ms")
        return
    try:
        val=int(val)
        if 200<=val<=10000:
            usbSerial.write(f"1:{val}\n".encode())
            messagebox.showinfo("OK",f"Velocidad: {val} ms")
        else:
            messagebox.showerror("Error",f"Fuera de rango: {val}")
    except:
        messagebox.showerror("Error","Valor no numérico")

Button(window,text="Validar",font=("Inter",14,"bold"),command=set_vel,
       bg="#4b6cb7",fg="white",bd=0,padx=20,pady=10).pack(pady=10)

# ---------------- Frames ----------------
left_frame = Frame(window,bg=col_izq,width=900,height=600)
right_frame = Frame(window,bg=col_der,width=900,height=600)
left_frame.pack(side=LEFT,fill=BOTH)
right_frame.pack(side=RIGHT,fill=BOTH,expand=True)
left_frame.pack_propagate(0)
right_frame.pack_propagate(0)

# ---------------- Botones izquierda ----------------
btn_frame_left = Frame(left_frame,bg=col_izq)
btn_frame_left.pack(pady=10)

def create_btn(master,text,command):
    return Button(master,text=text,command=command,font=button_font,
                  bg="#4b6cb7",fg="white",bd=0,padx=20,pady=15,width=18)

def iniClick(): usbSerial.write(b"3:i\n")
def stopClick(): usbSerial.write(b"3:p\n")
def reanClick(): usbSerial.write(b"3:r\n")

create_btn(btn_frame_left,"Iniciar transmisión",iniClick).grid(row=0,column=0,padx=10)
create_btn(btn_frame_left,"Parar transmisión",stopClick).grid(row=0,column=1,padx=10)
create_btn(btn_frame_left,"Reanudar",reanClick).grid(row=0,column=2,padx=10)

# ---------------- Plot Temp/Hum ----------------
fig_plot,ax_plot = plt.subplots(figsize=(7,4.5))
ax_plot.set_ylim(0,100)
ax_plot.set_title("Temperatura y Humedad")
line_temp,=ax_plot.plot(range(max_points),temps,label="Temperature")
line_hum,=ax_plot.plot(range(max_points),hums,label="Humidity")
line_med,=ax_plot.plot(range(max_points),temps_med,label="Avg. temp")
ax_plot.legend()
canvas_plot = FigureCanvasTkAgg(fig_plot,master=left_frame)
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
    window.after(100,update_plot)

# ---------------- Botones derecha ----------------
btn_frame_right = Frame(right_frame,bg=col_der)
btn_frame_right.pack(pady=10)
def os_man(): usbSerial.write(b"4:m\n")
def os_auto(): usbSerial.write(b"4:a\n")
create_btn(btn_frame_right,"OS Auto",os_auto).grid(row=0,column=0,padx=10)
create_btn(btn_frame_right,"OS Manual",os_man).grid(row=0,column=1,padx=10)

# ---------------- Radar ----------------
fig_rad,ax_rad = plt.subplots(subplot_kw={'polar':True},figsize=(7,4.5))
ax_rad.set_ylim(0,500)
ax_rad.set_thetamin(0)
ax_rad.set_thetamax(180)
ax_rad.set_theta_zero_location('W')
ax_rad.set_theta_direction(-1)
line_radar, = ax_rad.plot([],[], 'bo-', linewidth=2, alpha=0.6)
canvas_radar = FigureCanvasTkAgg(fig_rad,master=right_frame)
canvas_radar.get_tk_widget().pack(expand=True)
thetas = []
radios = []

def update_radar():
    global thetas,radios
    theta_now = np.deg2rad(angulo)
    r_now = min(max(latest_distance,0),500)
    thetas.append(theta_now)
    radios.append(r_now)
    if len(thetas)>20:
        thetas.pop(0)
        radios.pop(0)
    line_radar.set_data(thetas,radios)
    canvas_radar.draw()
    window.after(100,update_radar)

# ---------------- Loop GUI ----------------
window.after(100,update_plot)
window.after(500,update_radar)

def on_close():
    try: usbSerial.close()
    except: pass
    window.destroy()
    exit()
window.protocol("WM_DELETE_WINDOW",on_close)
window.mainloop()
