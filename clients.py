import socket
import threading
import logging
import argparse
import time

class thread_reading(threading.Thread):
    def __init__(self, name, socket_object):
        super().__init__()
        self.name = name
        self.socket_object = socket_object
        self.bufsize = 1024 * 8

    def run(self):
        result_buffer = b''
        while True:
            buf = self.socket_object.recv(self.bufsize)
            logging.info('{0} получил {1}'.format(self.name, buf))
            result_buffer += buf
            if b'1111' in result_buffer:
                break


def new_connection(client_number, host, port):
    socket_object = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    socket_object.connect((host, port))

    if socket_object.recv(1) != b'/':
        logging.error('Не удалось получить символ /')
    logging.info(f'{client_number} подключен')

    read_thread = thread_reading(client_number, socket_object)
    read_thread.start()

    s = b'message-1'
    logging.info(f'отправка {s}...')
    socket_object.send(s)
    time.sleep(1.2)

    s = b'message-2'
    logging.info(f'отправка {s}...')
    socket_object.send(s)
    time.sleep(1.2)

    s = b'$^0000$'
    logging.info(f'отправка {s}')
    socket_object.send(s)
    time.sleep(0.3)

    read_thread.join()
    socket_object.close()
    logging.info(f'{client_number} отключен')


def main():
    parser = argparse.ArgumentParser('')
    parser.add_argument('host', help='Название хоста')
    parser.add_argument('port', type=int, help='Номер порта')
    parser.add_argument('-n', '--num_clients', type=int,
                           default=1,
                           help='Число клиентов')
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG,
        format='%(levelname)s:%(asctime)s:%(message)s')

    num_conn = []
    for i in range(args.num_clients):
        name = 'клиент-{0}'.format(i)
        thread_conn = threading.Thread(target=new_connection,
                                 args=(name, args.host, args.port))
        thread_conn.start()
        num_conn.append(thread_conn)

    for conn in num_conn:
        conn.join()

if __name__ == '__main__':
    main()
