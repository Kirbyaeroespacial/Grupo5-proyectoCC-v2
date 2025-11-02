import unittest
from unittest.mock import Mock, patch
from src.ground_station.ground_station_v2 import GroundStation
import tkinter as tk

class TestGroundStation(unittest.TestCase):
    @patch('serial.Serial')
    def setUp(self, mock_serial):
        self.mock_serial = mock_serial
        self.station = GroundStation(port='COM3', baudrate=9600)
        
    def tearDown(self):
        if hasattr(self, 'station'):
            self.station.cleanup()
            self.station.root.destroy()

    def test_init(self):
        self.assertIsNotNone(self.station.root)
        self.assertEqual(self.station.port, 'COM3')
        self.assertEqual(self.station.baudrate, 9600)
        self.assertTrue(hasattr(self.station, 'data_buffer'))

    def test_process_valid_data(self):
        test_data = "1:4500:2300"  # 45% humidity, 23°C
        self.station.process_data(test_data)
        self.assertEqual(len(self.station.data_buffer['temp']), 1)
        self.assertEqual(len(self.station.data_buffer['hum']), 1)
        self.assertEqual(self.station.data_buffer['temp'][0], 23.0)
        self.assertEqual(self.station.data_buffer['hum'][0], 45.0)

    def test_process_invalid_data(self):
        test_data = "invalid:data:format"
        self.station.process_data(test_data)
        self.assertEqual(len(self.station.data_buffer['temp']), 0)
        self.assertEqual(len(self.station.data_buffer['hum']), 0)

    def test_send_command(self):
        self.station.send_command('i')
        self.mock_serial.return_value.write.assert_called_once_with('i\n'.encode())

    def test_start_transmission(self):
        self.station.start_transmission()
        self.assertEqual(self.station.status_var.get(), "Transmisión iniciada")
        self.mock_serial.return_value.write.assert_called_once_with('i\n'.encode())

    def test_pause_transmission(self):
        self.station.pause_transmission()
        self.assertEqual(self.station.status_var.get(), "Transmisión pausada")
        self.mock_serial.return_value.write.assert_called_once_with('p\n'.encode())

    def test_resume_transmission(self):
        self.station.resume_transmission()
        self.assertEqual(self.station.status_var.get(), "Transmisión reanudada")
        self.mock_serial.return_value.write.assert_called_once_with('r\n'.encode())

    @patch('serial.Serial')
    def test_failed_connection(self, mock_serial):
        mock_serial.side_effect = Exception("Test Error")
        with patch('tkinter.messagebox.showerror') as mock_error:
            station = GroundStation(port='COM3', baudrate=9600)
            self.assertIsNone(station.serial)
            mock_error.assert_called_once()
            self.assertEqual(station.status_var.get(), "Error: Test Error")

    def test_data_buffer_limit(self):
        # Test that buffer doesn't exceed maxlen
        for i in range(150):  # More than buffer size (100)
            self.station.process_data(f"1:{i*100}:{i*100}")
        
        self.assertEqual(len(self.station.data_buffer['temp']), 100)
        self.assertEqual(len(self.station.data_buffer['hum']), 100)

if __name__ == '__main__':
    unittest.main()
