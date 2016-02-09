import cmd
import select
import socket
import sys


class NatashaCLI(cmd.Cmd):

    prompt = '(natasha) '

    def do_help(self, what):
        return self.default('help %s' % what)

    def default(self, line):

        if line == 'EOF':
            return True

        self.stdout.sendall(line.strip() + '\n')

        while True:
            # if no data after n second, assume server's response is
            # displayed
            events = select.select([self.stdout], [], [], 0.1)

            if not events[0]:
                return False

            response = self.stdout.recv(4096)
            sys.stdout.write(response)


def main():

    while True:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

        try:
            sock.connect(('127.0.0.1', 4242))
        except IOError as exc:
            sys.stderr.write('connect error: %s\n' % exc)
            pass

        if len(sys.argv) > 1:
            return NatashaCLI(stdout=sock).onecmd(' '.join(sys.argv[1:]))

        try:
            return NatashaCLI(stdout=sock).cmdloop()
        except IOError as exc:
            sys.stderr.write('command error: %s\n' % exc)

        sock.close()


if __name__ == '__main__':
    main()
