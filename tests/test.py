import unittest
import socket
import time
import json

class TestPicoDB(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.host = '127.0.0.1'
        cls.port = 9000

    def send_cmd(self, cmd):
        """Вспомогательный метод для отправки команды серверу"""
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((self.host, self.port))
            s.sendall(cmd.encode('utf-8'))
            return s.recv(4096).decode('utf-8')

    def test_basic_set_get(self):
        self.assertEqual(self.send_cmd('SET key1 "Hello Pico"'), "OK")
        self.assertEqual(self.send_cmd('GET key1'), '"Hello Pico"')

    def test_math_bond(self):
        self.send_cmd('SET price 100')
        self.send_cmd('BOND total "price * 1.5"')

        self.assertEqual(float(self.send_cmd('GET total')), 150.0)

        self.send_cmd('SET price 200')
        self.assertEqual(float(self.send_cmd('GET total')), 300.0)

    def test_complex_types(self):
        self.send_cmd('DEL mylist') # очистим на всякий случай
        self.send_cmd('LPUSH mylist 10')
        self.send_cmd('LPUSH mylist "text"')
        res = self.send_cmd('GET mylist')
        self.assertIn('"text"', res)
        self.assertIn('10', res)

        self.send_cmd('HSET user:1 name "Valerii"')
        self.send_cmd('HSET user:1 age 25')
        user_data = self.send_cmd('GET user:1')
        self.assertIn('"name":"Valerii"', user_data)
        self.assertIn('"age":25', user_data)

    def test_ttl_expiration(self):
        self.send_cmd('SET temp "I will expire" EX 1')
        self.assertEqual(self.send_cmd('GET temp'), '"I will expire"')

        print("\nЖдем истечения TTL (1.5 сек)...")
        time.sleep(1.5)

        self.assertEqual(self.send_cmd('GET temp'), '(nil)')

if __name__ == '__main__':
    unittest.main()