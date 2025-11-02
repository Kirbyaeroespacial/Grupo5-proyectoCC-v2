# estacion_tierra.py  (guardar con este nombre para evitar shadowing)

# imports básicos
import serial
import threading
import time
from collections import deque

# tkinter
from tkinter import *
from tkinter import font

# matplotlib (asegurarse de usar el backend TkAgg antes de importar pyplot)
import matplotlib
matplotlib.use('TkAgg')
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg

plot_active = True

#Setup del serial
device = 'COM7'
usbSerial = None
try:
    usbSerial = serial.Serial(device, 9600, timeout=1)
except Exception as e:
    # Puerto no disponible; seguimos sin bloquear la ejecución de la GUI
    print(f"Warning: no se pudo abrir puerto serie {device}: {e}")
    alarm_message = f"No se pudo abrir puerto serie {device}: {e}"
    alarm_flag = True
    usbSerial = None

#Búfer de datos
max_points = 100
temps = deque([0]*max_points, maxlen=max_points)
hums = deque([0]*max_points, maxlen=max_points)
latest_data = {"temp": 0, "hum": 0}

# --- Nuevas variables para alarma/popup ---
alarm_flag = False        # cuando True, la GUI mostrará el popup
alarm_message = ""        # texto que mostrará el popup
alarm_popup_open = False  # evita abrir múltiples popups simultáneos

#Definimos la función read_serial que se encargara de leer los datos:
def read_serial():
    global plot_active, alarm_flag, alarm_message
    while True:
        linea = usbSerial.readline().decode('utf-8', errors='ignore').strip()
        if not linea:
            time.sleep(0.01)
            continue

        # Intentamos parsear protocolo con código al inicio: "1:hum:temp", "2:dist", "3:"
        if ":" in linea:
            parts = linea.split(":")
            # Si el primer trozo es un número, lo tomamos como código
            if parts[0].isdigit():
                try:
                    codigo = int(parts[0])
                except ValueError:
                    codigo = None

                if codigo == 1:
                    # Esperamos parts[1] = humedad*100, parts[2] = temp*100
                    try:
                        hum = float(parts[1]) / 100.0
                        temp = float(parts[2]) / 100.0
                        latest_data["temp"] = temp
                        latest_data["hum"] = hum
                        print(f"Serial (1): {temp:.2f} °C, {hum:.2f} %")
                    except (ValueError, IndexError):
                        # línea malformada -> marcar error de comunicación
                        alarm_message = "Línea recibida malformada (dato incompleto)."
                        alarm_flag = True
                elif codigo == 2:
                    # Ejemplo: distancia (aun no usado)
                    try:
                        dist = float(parts[1])
                        print(f"Serial (2) Distancia: {dist}")
                    except (ValueError, IndexError):
                        alarm_message = "Dato de distancia malformado."
                        alarm_flag = True
                elif codigo == 3:
                    # Error en captura de datos
                    print("Serial (3): ERROR en captura de datos (alarma)")
                    alarm_message = "Fallo en captura de datos: posible error de sensor o componente."
                    alarm_flag = True
                    plot_active = False
                else:
                    # Código desconocido: lo mostramos y se ignora
                    print("Serial: Codigo desconocido ->", linea)
            else:
                # Compatibilidad hacia atrás: formato antiguo "hum:temp"
                try:
                    ht = linea.split(":")
                    temp = float(ht[1]) / 100
                    hum = float(ht[0]) / 100
                    latest_data["temp"] = temp
                    latest_data["hum"] = hum
                    print(f"Serial (legacy): {temp:.2f} °C, {hum:.2f} %")
                except (ValueError, IndexError):
                    # Ignora lineas invalides pero señala posible error de comunicación
                    alarm_message = "Línea serial inválida (legacy)."
                    alarm_flag = True
        elif "e" in linea:
            # compatibilidad con antiguo 'e' -> error
            print("Serial: recibido 'e' -> desactivando plot (error antiguo)")
            alarm_message = "Error de sensor (mensaje antiguo 'e' recibido)."
            alarm_flag = True
            plot_active = False
        time.sleep(0.01)


# --- GUI: envolver en una función main() para evitar ejecutar en import ---
def main():
    global usbSerial, alarm_flag, alarm_message, alarm_popup_open, plot_active

    # Inicio GUI tinker
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
        global plot_active, alarm_flag, alarm_message
        if usbSerial is not None:
            try:
                usbSerial.write(b"i\n")
            except Exception as e:
                alarm_message = f"Error al escribir en puerto serie: {e}"
                alarm_flag = True
        else:
            alarm_message = "Puerto serie no disponible. Conecte el dispositivo o cambie la configuración."
            alarm_flag = True
        plot_active = True

    def stopClick():
        global plot_active, alarm_flag, alarm_message
        if usbSerial is not None:
            try:
                usbSerial.write(b"p\n")
            except Exception as e:
                alarm_message = f"Error al escribir en puerto serie: {e}"
                alarm_flag = True
        else:
            alarm_message = "Puerto serie no disponible."
            alarm_flag = True
        plot_active = False

    def reanClick():
        global plot_active, alarm_flag, alarm_message
        if usbSerial is not None:
            try:
                usbSerial.write(b"r\n")
            except Exception as e:
                alarm_message = f"Error al escribir en puerto serie: {e}"
                alarm_flag = True
        else:
            alarm_message = "Puerto serie no disponible."
            alarm_flag = True
        plot_active = True

    create_btn(btn_frame, "Iniciar transmisión", iniClick).grid(row=0, column=0, padx=10)
    create_btn(btn_frame, "Parar transmisión", stopClick).grid(row=0, column=1, padx=10)
    create_btn(btn_frame, "Reanudar", reanClick).grid(row=0, column=2, padx=10)


    # Gráfico dentro de interfaz
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

    # --- Función para mostrar popup estético (en hilo principal) ---
    def show_alarm_popup():
        nonlocal window
        global alarm_flag, alarm_popup_open, alarm_message
        if alarm_popup_open:
            return
        alarm_popup_open = True

        popup = Toplevel(window)
        popup.title("ALERTA SATÉLITE")
        popup.configure(bg="#1e1e2f")
        popup.transient(window)   # ventana sobre la principal
        popup.grab_set()          # modal simple
        # centrar popup
        w, h = 480, 200
        x = window.winfo_x() + (window.winfo_width() - w) // 2
        y = window.winfo_y() + (window.winfo_height() - h) // 2
        try:
            popup.geometry(f"{w}x{h}+{x}+{y}")
        except:
            popup.geometry(f"{w}x{h}")

        # Mensaje principal
        msg_font = font.Font(family="Inter", size=14, weight="bold")
        Label(popup, text="Se ha detectado un error", font=msg_font, bg="#1e1e2f", fg="#ff6b6b").pack(pady=(20,5))

        # Mensaje detallado (podemos poner alarm_message)
        detail_font = font.Font(family="Inter", size=12)
        detail = Label(popup, text=alarm_message, font=detail_font, bg="#1e1e2f", fg="#ffffff", wraplength=420, justify=CENTER)
        detail.pack(pady=(5,15))

        # Botón de cerrar con estilo coherrente
        def close_popup():
            global alarm_flag, alarm_popup_open
            alarm_flag = False
            alarm_popup_open = False
            popup.destroy()

        btn = create_btn(popup, "Cerrar", close_popup)
        btn.pack(pady=(0,10))

        # Asegurar foco en la ventana emergente
        popup.focus_force()


    # Actualizar gráfica periodicamente
    def update_plot():
        # Si hay alarma marcada, mostrar popup (en hilo principal): sólo una vez hasta que se cierre
        if alarm_flag and not alarm_popup_open:
            show_alarm_popup()

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
        global usbSerial
        try:
            if usbSerial is not None:
                usbSerial.close()
        except Exception:
            pass
        window.destroy()
        exit()

    window.protocol("WM_DELETE_WINDOW", on_close)

    # Si el puerto serie está disponible, arrancar el hilo lector
    if usbSerial is not None:
        threading.Thread(target=read_serial, daemon=True).start()

    window.mainloop()


if __name__ == "__main__":
    main()
