# estacion_tierra.py

import serial
import threading
import time
from collections import deque
import tkinter as tk
from tkinter import ttk, messagebox
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import matplotlib.animation as animation
from matplotlib import style

class GroundStation:
    def __init__(self, port='COM3', baudrate=9600):
        self.port = port
        self.baudrate = baudrate
        self.serial = None
        self.is_running = False
        self.data_buffer = {
            'temp': deque(maxlen=100),
            'hum': deque(maxlen=100)
        }
        
        # GUI Setup
        self.setup_gui()
        self.setup_plot()
        
        # Try to open serial port
        self.connect_serial()
        
        # Start reading thread if serial is open
        if self.serial:
            self.start_reading()

    def setup_gui(self):
        self.root = tk.Tk()
        self.root.title("Estación Tierra v2")
        self.root.geometry("800x600")
        
        # Control Frame
        control_frame = ttk.Frame(self.root)
        control_frame.pack(pady=10)
        
        ttk.Button(control_frame, text="Iniciar", command=self.start_transmission).pack(side=tk.LEFT, padx=5)
        ttk.Button(control_frame, text="Pausar", command=self.pause_transmission).pack(side=tk.LEFT, padx=5)
        ttk.Button(control_frame, text="Reanudar", command=self.resume_transmission).pack(side=tk.LEFT, padx=5)
        
        # Status Frame
        self.status_var = tk.StringVar(value="Desconectado")
        ttk.Label(self.root, textvariable=self.status_var).pack(pady=5)

    def setup_plot(self):
        self.fig, self.ax = plt.subplots(figsize=(8, 4))
        self.canvas = FigureCanvasTkAgg(self.fig, master=self.root)
        self.canvas.get_tk_widget().pack(pady=10, expand=True, fill=tk.BOTH)
        
        self.lines = {
            'temp': self.ax.plot([], [], label='Temperatura')[0],
            'hum': self.ax.plot([], [], label='Humedad')[0]
        }
        
        self.ax.set_ylim(0, 100)
        self.ax.set_xlim(0, 100)
        self.ax.legend()
        self.ax.grid(True)
        self.ax.set_title('Datos del Satélite')

    def connect_serial(self):
        try:
            self.serial = serial.Serial(self.port, self.baudrate, timeout=1)
            self.status_var.set(f"Conectado a {self.port}")
        except Exception as e:
            self.status_var.set(f"Error: {str(e)}")
            messagebox.showerror("Error de Conexión", str(e))

    def start_reading(self):
        self.is_running = True
        self.read_thread = threading.Thread(target=self.read_serial, daemon=True)
        self.read_thread.start()

    def read_serial(self):
        while self.is_running:
            if self.serial and self.serial.is_open:
                try:
                    if self.serial.in_waiting:
                        line = self.serial.readline().decode('utf-8').strip()
                        self.process_data(line)
                except Exception as e:
                    print(f"Error leyendo serial: {e}")
            time.sleep(0.1)

    def process_data(self, data):
        try:
            if ':' in data:
                parts = data.split(':')
                if len(parts) == 3 and parts[0] == '1':
                    hum = float(parts[1]) / 100.0
                    temp = float(parts[2]) / 100.0
                    self.data_buffer['temp'].append(temp)
                    self.data_buffer['hum'].append(hum)
                    self.update_plot()
        except Exception as e:
            print(f"Error procesando datos: {e}")

    def update_plot(self):
        for name, line in self.lines.items():
            line.set_data(range(len(self.data_buffer[name])), list(self.data_buffer[name]))
        self.ax.relim()
        self.ax.autoscale_view()
        self.canvas.draw()

    def send_command(self, cmd):
        if self.serial and self.serial.is_open:
            try:
                self.serial.write(f"{cmd}\n".encode())
            except Exception as e:
                messagebox.showerror("Error", f"Error enviando comando: {e}")

    def start_transmission(self):
        self.send_command('i')
        self.status_var.set("Transmisión iniciada")

    def pause_transmission(self):
        self.send_command('p')
        self.status_var.set("Transmisión pausada")

    def resume_transmission(self):
        self.send_command('r')
        self.status_var.set("Transmisión reanudada")

    def run(self):
        self.root.mainloop()

    def cleanup(self):
        self.is_running = False
        if self.serial:
            self.serial.close()

if __name__ == "__main__":
    station = GroundStation()
    try:
        station.run()
    finally:
        station.cleanup()
