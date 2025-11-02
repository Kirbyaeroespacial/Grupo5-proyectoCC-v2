import unittest
from unittest.mock import Mock, patch, MagicMock
import sys
import os
import threading
from tkinter import Tk, Toplevel
import time
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from src.ground_station.ground_station_v3 import main

class TestGroundStationV3(unittest.TestCase):
    @patch('serial.Serial')
    def setUp(self, mock_serial):
        self.mock_serial = mock_serial
        self.root = None

    def tearDown(self):
        if self.root:
            self.root.destroy()

    @patch('tkinter.Tk')
    @patch('threading.Thread')
    def test_main_initialization(self, mock_thread, mock_tk):
        mock_window = MagicMock()
        mock_tk.return_value = mock_window
        
        main()
        
        # Check that window was configured properly
        mock_window.title.assert_called_with("Control Satélite")
        mock_window.geometry.assert_called_with("900x600")
        self.assertTrue(mock_thread.called)

    @patch('serial.Serial')
    def test_read_serial_valid_data(self, mock_serial):
        mock_serial.readline.return_value = b"1:4500:2300\n"
        from src.ground_station.ground_station_v3 import read_serial, latest_data
        
        # Start reading (will run in loop, so we'll need to break)
        read_thread = threading.Thread(target=read_serial)
        read_thread.daemon = True
        read_thread.start()
        
        # Give it time to process
        time.sleep(0.1)
        
        # Check the processed data
        self.assertEqual(latest_data["temp"], 23.0)
        self.assertEqual(latest_data["hum"], 45.0)

    @patch('serial.Serial')
    def test_read_serial_error_code(self, mock_serial):
        mock_serial.readline.return_value = b"3:\n"
        from src.ground_station.ground_station_v3 import read_serial, alarm_flag, alarm_message, plot_active
        
        import src.ground_station.ground_station_v3 as gs
        # Reset globals
        gs.alarm_flag = False
        gs.alarm_message = ""
        gs.plot_active = True
        
        read_thread = threading.Thread(target=read_serial)
        read_thread.daemon = True
        read_thread.start()
        
        time.sleep(0.1)
        
        self.assertTrue(alarm_flag)
        self.assertFalse(plot_active)
        self.assertIn("Fallo en captura de datos", alarm_message)

    @patch('serial.Serial')
    def test_read_serial_distance_data(self, mock_serial):
        mock_serial.readline.return_value = b"2:150\n"
        from src.ground_station.ground_station_v3 import read_serial
        
        read_thread = threading.Thread(target=read_serial)
        read_thread.daemon = True
        read_thread.start()
        
        time.sleep(0.1)
        
        # No error should be raised

    @patch('serial.Serial')
    def test_read_serial_legacy_format(self, mock_serial):
        mock_serial.readline.return_value = b"4500:2300\n"
        from src.ground_station.ground_station_v3 import read_serial, latest_data
        
        read_thread = threading.Thread(target=read_serial)
        read_thread.daemon = True
        read_thread.start()
        
        time.sleep(0.1)
        
        self.assertEqual(latest_data["temp"], 23.0)
        self.assertEqual(latest_data["hum"], 45.0)

    @patch('serial.Serial')
    def test_button_commands(self, mock_serial):
        mock_serial.return_value = MagicMock()
        
        from src.ground_station.ground_station_v3 import iniClick, stopClick, reanClick
        
        # Test Iniciar
        iniClick()
        mock_serial.return_value.write.assert_called_with(b"i\n")
        
        # Test Parar
        stopClick()
        mock_serial.return_value.write.assert_called_with(b"p\n")
        
        # Test Reanudar
        reanClick()
        mock_serial.return_value.write.assert_called_with(b"r\n")

if __name__ == '__main__':
    unittest.main()
