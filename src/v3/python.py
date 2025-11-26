# ground_gui.py
import time
import threading
import re
from collections import deque
from tkinter import *
from tkinter import font, messagebox
import matplotlib
matplotlib.use("TkAgg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import numpy as np

# pyserial import con manejo amigable si falta
try:
    import serial
    import serial.tools.list_ports
except Exception as e:
    serial = None
    print("Aviso: pyserial no encontrado. Instala 'pyserial' para conectar con Arduino.")
    print("pip install pyserial")
    # seguiremos en modo desconectado

# ---------------- Configurables ----------------
device = 'COM7'   # Cambia esto si es necesario
baudrate = 9600

# --------------- Estado global -----------------
plot_active = True
max_points = 100
temps = deque([0]*max_points, maxlen=max_points)
hums = deque([0]*max_points, maxlen=max_points)
temps_med = deque([0]*max_points, maxlen=max_points)
latest_data = {"temp": 0, "hum": 0}
latest_distance = 0
angulo = 90
latest_temp_med = 0
thetas = []
radios = []
total_corrupted = 0

last_data_received = time.time()
connection_status = "Esperando..."
connection_lock = threading.Lock()
orbit_x = []
orbit_y = []
orbit_lock = threading.Lock()

regex_orbit = re.compile(r"Position: \(X: ([\d\.-]+) m, Y: ([\d\.-]+) m, Z: ([\d\.-]+) m\)")

# ------------- Serial wrapper -------------------
class SerialWrapper:
    def __init__(self, port, baud, timeout=1):
        self.connected = False
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self.ser = None
        if serial is None:
            return
        try:
            self.ser = serial.Serial(port, baud, timeout=timeout)
            self.connected = True
            print(f"Puerto serial abierto: {port} @ {baud}")
        except Exception as e:
            print(f"No se pudo abrir puerto {port}: {e}")
            self.connected = False

    def readline(self):
        if not self.connected:
            time.sleep(0.05)
            return b''
        try:
            return self.ser.readline()
        except Exception as e:
            print("Error leyendo serial:", e)
            self.connected = False
            return b''

    def write(self, data: bytes):
        if not self.connected:
            print("Serial no conectado: intento de envío descartado:", data)
            return
        try:
            self.ser.write(data)
        except Exception as e:
            print("Error enviando serial:", e)
            self.connected = False

    def close(self):
        if self.connected and self.ser:
            try:
                self.ser.close()
            except:
                pass
        self.connected = False

    @staticmethod
    def list_ports():
        if serial is None:
            return []
        ports = serial.tools.list_ports.comports()
        return [p.device for p in ports]

# Intentar abrir puerto; si falla, el wrapper seguirá existiendo pero desconectado
usbSerial = SerialWrapper(device, baudrate, timeout=1)

# --------------- Checksum -----------------------
def calc_checksum(msg: str) -> str:
    if isinstance(msg, str):
        b = msg.encode('ascii', 'ignore')
    else:
        b = bytes(msg)
    xor_sum = 0
    for vb in b:
        xor_sum ^= vb
    return format(xor_sum, '02X').upper()

def send_command_with_checksum(command: str):
    checksum = calc_checksum(command)
    full_msg = f"{command}*{checksum}\n"
    usbSerial.write(full_msg.encode('ascii'))
    print(f"✓ Enviado: {full_msg.strip()}")

def validate_message(line: str):
    line = line.strip()
    if '*' not in line:
        return False, ""
    parts = line.split('*', 1)
    if len(parts) != 2:
        return False, ""
    msg = parts[0]
    recv_checksum = parts[1].strip()
    calc_check = calc_checksum(msg)
    if recv_checksum.upper() == calc_check.upper():
        return True, msg
    else:
        print(f"⚠ Checksum inválido: esperado {calc_check}, recibido {recv_checksum}  | raw='{line}'")
        return False, ""

# -------------- Monitor conexión ---------------
def monitor_connection():
    global connection_status
    while True:
        time_since_last = time.time() - last_data_received
        with connection_lock:
            if time_since_last < 5:
                connection_status = "🟢 CONECTADO"
            elif time_since_last < 10:
                connection_status = "🟡 SEÑAL DÉBIL"
            else:
                connection_status = "🔴 SIN SEÑAL"
        time.sleep(1)

threading.Thread(target=monitor_connection, daemon=True).start()

# ----------------- Lectura Serial ----------------
def read_serial():
    global plot_active, latest_distance, angulo, latest_temp_med, total_corrupted, last_data_received
    global orbit_x, orbit_y

    while True:
        try:
            raw = usbSerial.readline()
            if not raw:
                time.sleep(0.01)
                continue
            try:
                linea = raw.decode('utf-8', errors='ignore').strip()
            except:
                linea = raw.decode('latin1', errors='ignore').strip()
        except Exception as e:
            print("Error leyendo serial (externo):", e)
            time.sleep(0.1)
            continue

        if not linea:
            time.sleep(0.01)
            continue

        # actualizar timestamp
        last_data_received = time.time()

        # logs y debug
        if linea.startswith("->") or linea.startswith("<-") or linea.startswith("===") or linea.startswith("⚠") or linea.startswith("✓"):
            print(f"[LOG] {linea}")
            time.sleep(0.01)
            continue

        # órbita
        match = regex_orbit.search(linea)
        if match:
            try:
                x = float(match.group(1)); y = float(match.group(2))
                with orbit_lock:
                    orbit_x.append(x); orbit_y.append(y)
                    if len(orbit_x) > 1000:
                        orbit_x.pop(0); orbit_y.pop(0)
                print(f"🛰️ Orbital: X={x:.0f}, Y={y:.0f}")
            except ValueError:
                pass
            time.sleep(0.01)
            continue

        # validar checksum si tiene
        if '*' in linea:
            valid, clean_msg = validate_message(linea)
            if not valid:
                total_corrupted += 1
                print(f"⚠ Mensaje corrupto descartado (Total: {total_corrupted}) | raw='{linea}'")
                time.sleep(0.01)
                continue
            linea = clean_msg

        parts = linea.split(':')
        try:
            if len(parts) >= 2 and parts[0] in ('1','2','3','4','5','6','7','8','9','67','99'):
                idn = parts[0]
                if idn == '1':
                    if len(parts) >= 3:
                        try:
                            hum = int(parts[1]) / 100.0
                            temp = int(parts[2]) / 100.0
                            latest_data["temp"] = temp
                            latest_data["hum"] = hum
                            print(f"📊 Temp: {temp:.2f}°C, Hum: {hum:.2f}%")
                        except ValueError:
                            pass
                elif idn == '2':
                    try:
                        latest_distance = int(parts[1])
                        print(f"📏 Distancia: {latest_distance} mm")
                    except ValueError:
                        pass
                elif idn == '3':
                    plot_active = False
                    messagebox.showerror("Error transmisión", f"Error: {':'.join(parts[1:])}")
                elif idn == '4':
                    messagebox.showerror("Error sensor", "⚠ Error en sensor temp/hum")
                elif idn == '5':
                    messagebox.showerror("Error sensor", "⚠ Error en sensor distancia")
                elif idn == '6':
                    try:
                        angulo = int(parts[1])
                    except ValueError:
                        messagebox.showerror("Error ángulo", "Valor incorrecto")
                elif idn == '7':
                    try:
                        latest_temp_med = int(parts[1]) / 100.0
                        print(f"📈 Temp media: {latest_temp_med:.2f}°C")
                    except ValueError:
                        pass
                elif idn == '8':
                    messagebox.showwarning("⚠ Alta temperatura!", "¡PELIGRO! Temp media >100°C")
                elif idn == '9':
                    # ya tratado arriba si viene como Position:
                    pass
                elif idn == '67':
                    pass
                elif idn == '99':
                    try:
                        corrupted = int(parts[1]); total_corrupted += corrupted
                        print(f"📊 [CHECKSUM] Descartados (notificados): {corrupted} | Total: {total_corrupted}")
                    except ValueError:
                        pass
        except Exception as e:
            print("⚠ Parse error:", e)

        time.sleep(0.01)

threading.Thread(target=read_serial, daemon=True).start()

# ------------------- GUI ------------------------
window = Tk()
window.title("Control Satélite")
window.geometry("1200x720")

title_font = font.Font(family="Inter", size=20, weight="bold")
button_font = font.Font(family="Inter", size=12, weight="bold")
col_izq = "#1e292f"; col_der = "#31434d"
window.configure(bg="#1e1e2f")

# top frame
title_frame = Frame(window, bg="#1e1e2f"); title_frame.pack(pady=8, fill=X)
Label(title_frame, text="Control Satélite", font=title_font, bg="#1e1e2f", fg="white").pack(side=LEFT, padx=10)
status_label = Label(title_frame, text=connection_status, font=("Inter", 12, "bold"), bg="#1e1e2f", fg="white")
status_label.pack(side=LEFT, padx=10)

def update_status_label():
    with connection_lock:
        status_label.config(text=connection_status)
    window.after(1000, update_status_label)
update_status_label()

# left / right frames
left_frame = Frame(window, bg=col_izq, width=700, height=600); left_frame.pack(side=LEFT, fill=BOTH, expand=False)
right_frame = Frame(window, bg=col_der, width=500, height=600); right_frame.pack(side=RIGHT, fill=BOTH, expand=True)
left_frame.pack_propagate(0); right_frame.pack_propagate(0)

# Simple controls
def send_iniciar(): send_command_with_checksum("3:i"); print("✓ Comando INICIAR enviado")
def send_parar(): send_command_with_checksum("3:p"); print("✓ Comando PARAR enviado")
def send_reanudar(): send_command_with_checksum("3:r"); print("✓ Comando REANUDAR enviado")
def set_mode_auto(): send_command_with_checksum("4:a"); print("✓ Modo AUTO enviado")
def set_mode_man(): send_command_with_checksum("4:m"); print("✓ Modo MANUAL enviado")

btnFrame = Frame(left_frame, bg=col_izq); btnFrame.pack(pady=10)
Button(btnFrame, text="Iniciar", command=send_iniciar, font=button_font, bg="#4b6cb7", fg="white").grid(row=0,column=0,padx=6)
Button(btnFrame, text="Parar", command=send_parar, font=button_font, bg="#4b6cb7", fg="white").grid(row=0,column=1,padx=6)
Button(btnFrame, text="Reanudar", command=send_reanudar, font=button_font, bg="#4b6cb7", fg="white").grid(row=0,column=2,padx=6)

# Plot temp/hum
fig_plot, ax_plot = plt.subplots(figsize=(7,3.8))
ax_plot.set_ylim(0,100)
line_temp, = ax_plot.plot(range(max_points), list(temps), label="Temp")
line_hum, = ax_plot.plot(range(max_points), list(hums), label="Hum")
line_med, = ax_plot.plot(range(max_points), list(temps_med), label="Avg Temp", linestyle='--')
ax_plot.legend()
canvas_plot = FigureCanvasTkAgg(fig_plot, master=left_frame)
canvas_plot.get_tk_widget().pack(pady=8)

def update_plot():
    temps.append(latest_data["temp"])
    hums.append(latest_data["hum"])
    temps_med.append(latest_temp_med)
    line_temp.set_ydata(list(temps))
    line_hum.set_ydata(list(hums))
    line_med.set_ydata(list(temps_med))
    ax_plot.relim(); ax_plot.autoscale_view()
    canvas_plot.draw_idle()
    window.after(150, update_plot)

window.after(150, update_plot)

# Radar simple (derecha)
fig_r, ax_r = plt.subplots(subplot_kw={'polar': True}, figsize=(5,4))
ax_r.set_ylim(0,500); ax_r.set_thetamin(0); ax_r.set_thetamax(180)
ax_r.set_theta_zero_location('W'); ax_r.set_theta_direction(-1)
linea_radar, = ax_r.plot([], [], 'bo-')
canvas_radar = FigureCanvasTkAgg(fig_r, master=right_frame)
canvas_radar.get_tk_widget().pack(expand=True, fill=BOTH)

def update_radar():
    global thetas, radios
    theta_now = np.deg2rad(angulo)
    r_now = min(max(latest_distance, 0), 500)
    thetas.append(theta_now); radios.append(r_now)
    if len(thetas) > 20: thetas.pop(0); radios.pop(0)
    linea_radar.set_data(thetas, radios)
    canvas_radar.draw_idle()
    window.after(150, update_radar)

window.after(150, update_radar)

# Port info button
def show_ports():
    if serial is None:
        messagebox.showerror("pyserial faltante", "Instala pyserial: pip install pyserial")
        return
    ports = SerialWrapper.list_ports()
    if not ports:
        messagebox.showinfo("Puertos", "No se encontraron puertos serie.\nConecta el dispositivo y pulsa Aceptar para reintentar.")
    else:
        messagebox.showinfo("Puertos disponibles", "\n".join(ports))

Button(title_frame, text="Puertos", command=show_ports, bg="#6b8dd6", fg="white").pack(side=RIGHT, padx=10)

# Close handler
def on_close():
    try:
        usbSerial.close()
    except:
        pass
    window.destroy()
    try:
        import sys; sys.exit(0)
    except:
        pass

window.protocol("WM_DELETE_WINDOW", on_close)
window.mainloop()
