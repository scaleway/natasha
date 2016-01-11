import cmd
import socket
import sys
import time


class NatashaCLI(cmd.Cmd):

    prompt = '(natasha) '

    def __init__(self, *args, **kwargs):
        cmd.Cmd.__init__(self, *args, **kwargs)
        self.sock = None

    def cmdloop(self, sock, *args, **kwargs):
        self.sock = sock
        cmd.Cmd.cmdloop(self, *args, **kwargs)

    def do_help(self, what):
        return self.default('help %s' % what)

    def default(self, line):

        if line == 'EOF':
            return True

        self.sock.sendall(line.strip() + '\n')
        response = self.sock.recv(4096)
        if not len(response):
            return True

        sys.stdout.write(response)


def main():
    socket_name = '/var/run/natasha.socket'

    while True:
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)

        try:
            sock.connect(socket_name)
        except IOError as exc:
            pass

        try:
            return NatashaCLI().cmdloop(sock)
        except IOError as exc:
            sys.stderr.write('CLI error: %s\n' % exc)

        sock.close()


if __name__ == '__main__':
    main()
