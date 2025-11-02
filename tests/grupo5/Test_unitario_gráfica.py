def read_serial():
        global plot_active
        while True:
            linea = usbSerial.readline().decode('utf-8').strip()
            if ":" in linea:
                plot_active = True
                ht = linea.split(":")
                try:
                    temp = float(ht[1]) / 100
                    hum = float(ht[0]) / 100
                    latest_data["temp"] = temp
                    latest_data["hum"] = hum
                    print(f"Serial: {temp:.2f} °C, {hum:.2f} %")
                except (ValueError, IndexError):
                    pass
            elif "e" in linea:
                plot_active = False
            print (linea) #<----------  ESTO DE AQUÍ
            time.sleep(0.01)

#He integrado print(linea) en la definición de read_serial del código en python principal para que me ponga en el terminal los datos sin procesar que envia el arduino satélite.
#Me ha permitido encontrar un error donde había escrito 'e' en vez de "e", por lo que el programa de python no entendia que había un error.
