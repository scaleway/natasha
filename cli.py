import cmd
import socket
import sys
import time


class NatashaCLI(cmd.Cmd):

    prompt = '(natasha) '

    def do_help(self, what):
        return self.default('help %s' % what)

    def default(self, line):

        if line == 'EOF':
            return True

        self.stdout.sendall(line.strip() + '\n')
        response = self.stdout.recv(4096)
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

        if len(sys.argv) > 1:
            return NatashaCLI(stdout=sock).onecmd(' '.join(sys.argv[1:]))

        try:
            return NatashaCLI(stdout=sock).cmdloop()
        except IOError as exc:
            sys.stderr.write('CLI error: %s\n' % exc)

        sock.close()


if __name__ == '__main__':
    main()
