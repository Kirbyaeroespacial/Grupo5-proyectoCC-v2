#Inicio de las importaciones de elementos
import time
import threading
import tkinter as tk
from collections import deque
from tkinter import *
from tkinter import font, messagebox 
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import numpy as np
import matplotlib
import os
import sys
import re
import serial
matplotlib.use("TkAgg")


ventana = tk.Tk()


#INICIO CONFIGURACIÓN UI
resolx = 1900
resoly = 800
resolxs = 1280
resolys = 720
resolucionvp = (f"{resolx}x{resoly}")
resolucionvs = (f"{resolxs}x{resolys}")
col_gen = "#24243b"
col_izq = "#27394a"
col_der = "#31434d"
col_btn = "#4b6cb7"
col_btn_actv = "#4b6dd6"
col_placeholder = "#aaaaaa"
fg = "#ffffff"
fuente_titulo = font.Font(family ="Inter", size = 32, weight="bold")
fuente_btn = font.Font(family="Inter", size=14, weight="bold")
#Fin configuración UI



#Inicio inicializació serial
groundstt = 'COM7'
usbSerial = serial.Serial(groundstt, 9600, timeout = 1)
#Fin inicialización serial (¡NO OLVIDAR DESCOMENTAR AL ACABAR DEBUG)



#Definición de variables globales:
ult_datos = {"temperatura":0, "humedad":0}
id = '0'
partes = '0'

#para el gráfico de temp/hum
pt_max = 100
temperaturas = deque([0]*pt_max, maxlen=pt_max)
humedades = deque([0]*pt_max, maxlen=pt_max)
temps_med = deque([0]*pt_max, maxlen=pt_max) 
ult_dist = 0
angulo = 0
ult_tmed = 0
plot_active = False

#listas para trail sonar
angulos = []
distancias = []



#INICIO DEFINICIÓN DE  PROTOCOLO
def procesar_recepcion ():
    global id, partes
    strecep = usbSerial.readline().decode('utf-8').strip()
    if not strecep:
        time.sleep(0.01)
        return 
    partes = strecep.split(':')
    try:
        if len(partes) >= 2 and partes[0] in ('1','2','3','4','5','6','7','8', '9'):
            id = partes[0]
    except Exception as e:
        print("Parse error:", e)

#Temperatura y humedad
def procesar_th (partes):
        global ult_datos
        if len(partes) >=3:
            try:
                humedad = int(partes[1])/ 100.0
                temperatura = int(partes[2])/100.0
                ult_datos["temperatura"] = temperatura
                ult_datos["humedad"] = humedad
                #ELIMINAR ESTO AL FINALIZAR DEBUG
                print(f"Serial: {temperatura:.2f}ºC, {humedad:.2f}% rel")
            except ValueError:
                pass

#Distancia
def procesar_dist (partes):
    global ult_dist
    try:
        ult_dist = int(partes[1])
        print(f"Distancia recibida {ult_dist}mm")
    except ValueError:
        pass
#Error comunicación
def error_tms ():
    global plot_active
    plot_active = False
    messagebox.showerror("Error comunicación", f"Error en la comunicación de datos, comprobar LoRa o conexión directa")
#Error en temperatura/humedad
def err_th ():
    messagebox.showerror("Error sensor", f"Error en el sensor de temperatura/humedad")
#Error sónar
def err_dist ():
    messagebox.showerror("Error sensor", f"Error en el sensor de distáncia (sonar)")
#Angulo sensor
def act_angle (partes):
    global angulo
    try:
        angulo = int(partes[1])
    except ValueError:
        messagebox.showerror("Error ángulo", "Error al recibir el ángulo del sonar")

def avg_tmp (partes):
    global ult_tmed
    try:
        ult_tmed = int(partes[1])/100.0
    except ValueError:
        pass

def er_ht():
    messagebox.showinfo("Alta temperatura", f"Cuidado, ¡alta temperatura exterior detectada!")

def posicion_sat():
    print("Work in progress")

def procesar_llegada():
    global ult_datos, plot_active, ult_dist, angulo, ult_tmed
    while True:
        try:
            procesar_recepcion()
        except Exception as e:
            print("Error serial", e)
            continue
        if not id:
            continue
        if id == '1':
            procesar_th(partes)
        elif id == '2':
            procesar_dist(partes)
        elif id == '3':
            error_tms()
        elif id == '4':
            err_th()
        elif id == '5':
            err_dist()
        elif id == '6':
            act_angle(partes)
        elif id == '7':
            avg_tmp(partes)
        elif id == '8':
            er_ht()
        elif id == '9':
            posicion_sat()
        time.sleep(0.01)
threading.Thread(target = procesar_llegada, daemon = True).start()
#FIN DEFINICIÓN DEL PROTOCOLO DE RECEPCIÓN


#INICIO DEFINICIÓN PROTOCOLOS DE ENVÍO
def env_veltrans (vel_datos_raw):
    try:
        vel_datos = int(vel_datos_raw)
        if 200 <= vel_datos <= 10000:
            usbSerial.write(f"1:{vel_datos}\n".encode())
            #ELIMINAR AL FINALIZAR DEBUG
            print("1:", vel_datos)
        else:
            messagebox.showerror("Error de datos", f"Número fuera de rango: {vel_datos}")
    except ValueError:
        messagebox.showerror("Error de datos", f"Valor no numérico: {vel_datos_raw}")
        return 1
    
def leer_vel ():
    vel_datos_raw = entry.get()
    if vel_datos_raw == placeholder or vel_datos_raw == "":
        messagebox.showerror("Error", "Introduzca un valor en ms entre 200 y 10000")
        return
    
    env_veltrans(vel_datos_raw)


def iniciar_env():
    global plot_active
    usbSerial.write(b"3:i\n")
    plot_active = True

def parar_env():
    global plot_active
    usbSerial.write(b"3:p\n")

def reanudar_env():
    global plot_active
    usbSerial.write(b"3:r\n")

def os_man():
    usbSerial.write(b"4:m\n")

def os_auto():
    usbSerial.write(b"4:a\n")
#FIN DEFINICIÓN PROTOCOLOS DE ENVÍO


#INICIO OTRAS DEFINICIONES ÚTILES PARA LA UI
def crear_btn(master, text, command):
    return Button(master, text = text, command = command, font = fuente_btn, bg= col_btn, fg="white", activebackground=col_btn_actv, activeforeground="white", bd=0, relief= RIDGE, padx = 20, pady = 15, width=18)

def entrada_cajatxt(event):
    if entry.get() == placeholder:
        entry.delete(0, END)
        entry.config(fg="black")

def desenfoque_cajatxt(event):
    if entry.get() == "":
        entry.insert(0, placeholder)
        entry.config(fg="gray")

def on_close():
    try:
        usbSerial.close()
    except:
        pass
    ventana.destroy()
    exit()
#FIN OTRAS DEFINICIONES (UI)


#INICIO DEFINICIONES ACTUALIZAR GRÁFICAS
#Temp/hum
def update_plot():
    temperaturas.append(ult_datos["temperatura"])
    humedades.append(ult_datos["humedad"])
    temps_med.append(ult_tmed)  # corregido

    line_temp.set_visible(plot_active)
    line_hum.set_visible(plot_active)
    line_med.set_visible(plot_active)

    line_temp.set_ydata(temperaturas)
    line_hum.set_ydata(humedades)
    line_med.set_ydata(temps_med)

    ax_plot.relim()
    ax_plot.autoscale_view()
    canvas_plot.draw()
    ventana.after(100, update_plot)

#Radar
def update_radar():
    global ult_dist, angulo, angulos, distancias
    print(angulo)
    theta_now = np.deg2rad(angulo)
    r_now = min(max(ult_dist, 0), max_distance)
    angulos.append(theta_now)
    distancias.append(r_now)
    if len(angulos) > 20:
        angulos.pop(0)
        distancias.pop(0)
    linea_radar.set_data(angulos, distancias)
    canvas_radar.draw()
    ventana.after(100, update_radar)
#FIN DEFINICIONES ACTUALIZAR GRÁFICAS



#INICIO TKINTER
ventana.title("Control Satélite")
ventana.geometry(resolucionvp)
ventana.configure(bg=col_gen)
ventana.resizable(False, False)

#SECCIONES DEL PROGRAMA
frame_top = Frame(ventana, bg=col_gen)
frame_top.pack(fill=X, pady = 10, padx =10)
left_frame = Frame(ventana, bg=col_izq, width=900, height=600)
left_frame.pack(side=LEFT, fill=BOTH)
right_frame = Frame(ventana, bg=col_der, width=900, height=600)
right_frame.pack(side=RIGHT, fill=BOTH, expand=True)
left_frame.pack_propagate(0)
right_frame.pack_propagate(0)
btn_frame_left = Frame(left_frame, bg=col_izq)
btn_frame_left.pack(pady=10)
btn_frame_right = Frame(right_frame, bg=col_der)
btn_frame_right.pack(pady=10)

#Título :
Title = Label(frame_top, text ="Control Satélite", font = fuente_titulo, bg = col_gen, fg = fg)
Title.pack(pady=10)

#La caja de texto para cambiar vel_datos
entry = Entry(frame_top, font=("Inter", 14))
entry.pack(pady=20, ipadx=80, ipady=5)
placeholder = "Tiempo entre datos (ms)"
entry.insert(0, placeholder)
entry.bind("<FocusIn>", entrada_cajatxt)
entry.bind("<FocusOut>", desenfoque_cajatxt)

#UI IZQUIERDA
crear_btn(frame_top,"Enviar velocidad", leer_vel).pack(padx=10, pady=15)
crear_btn(btn_frame_left, "Iniciar transmisión", iniciar_env).grid(row=0, column=0, padx=10)
crear_btn(btn_frame_left, "Parar transmisión", parar_env).grid(row=0, column=1, padx=10)
crear_btn(btn_frame_left, "Reanudar", reanudar_env).grid(row=0, column=2, padx=10)

#UI DERECHA
crear_btn(btn_frame_right, "OS Auto", os_auto).grid(row=0, column=0, padx=10)
crear_btn(btn_frame_right, "OS Manual", os_man).grid(row=0, column=1, padx=10)
#FIN TKINTER



#INICIO CONFIGURACIÓN GRÁFICAS
#Temp/hum
fig_plot, ax_plot = plt.subplots(figsize=(7, 4.5))
ax_plot.set_ylim(0, 100)
ax_plot.set_title("Temperatura y Humedad")
line_temp, = ax_plot.plot(range(pt_max), temperaturas, label="Temperatura")
line_hum, = ax_plot.plot(range(pt_max), humedades, label="Humedad")
line_med, = ax_plot.plot(range(pt_max), temps_med, label="Temp. med.")  # corregido
ax_plot.legend()
canvas_plot = FigureCanvasTkAgg(fig_plot, master=left_frame)
canvas_plot.get_tk_widget().pack(pady=20)

#Sonar
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
#FIN CONFIGURACIÓN GRÁFICAS



ventana.after(100, update_plot)
ventana.after(500, update_radar)
ventana.protocol("WM_DELETE_WINDOW", on_close)
ventana.mainloop()