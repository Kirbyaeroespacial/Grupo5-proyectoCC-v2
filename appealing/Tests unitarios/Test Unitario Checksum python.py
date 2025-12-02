import serial

def calc_checksum(msg: str) -> str:
    xor_sum = 0
    for c in msg:
        xor_sum ^= ord(c)
    hexstr = format(xor_sum, "02X")
    return hexstr

def send_packet(ser, tipo, payload):
    msg = f"{tipo}:{payload}"
    chk = calc_checksum(msg)
    full = f"{msg}*{chk}\n"
    ser.write(full.encode())
    print("->", full.strip())

ser = serial.Serial("COM5", 9600)

print("Python mínimo listo")

while True:
    # recibir
    if ser.in_waiting:
        line = ser.readline().decode().strip()
        print("<-", line)

    # enviar manual
    txt = input("Payload: ")
    if txt:
        send_packet(ser, 3, txt)
