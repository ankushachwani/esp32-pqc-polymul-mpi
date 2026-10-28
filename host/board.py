"""Serial driver for the benchmark firmware. Runs on the machine the ESP32 is plugged into (the Pi). Same reset sequence as the RSA project."""
import time
import serial

PORT = '/dev/ttyUSB0'


class Board:
    def __init__(self, port=PORT, baud=115200, boot_timeout=20):
        self.s = serial.Serial(port, baud, timeout=1)
        self.reset(boot_timeout)

    def reset(self, timeout=20):
        s = self.s; s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False
        t0 = time.time()
        while time.time() - t0 < timeout:
            ln = s.readline().decode(errors='replace').strip()
            if ln.startswith('READY pqc-mpi'):
                quiet = time.time()                       # opening the port can reset the board a second time: wait for silence
                while time.time() - quiet < 1.5:
                    if s.readline(): quiet = time.time()
                s.reset_input_buffer(); return
        raise RuntimeError('board did not announce READY pqc-mpi')

    def cmd(self, line, timeout=120):
        """Send one command, return the reply lines up to END. Raises on ERR or timeout."""
        self.s.write((line + '\n').encode()); self.s.flush()
        out = []; err = None; t0 = time.time()
        while time.time() - t0 < timeout:
            ln = self.s.readline().decode(errors='replace').strip()
            if not ln: continue
            if ln == 'END':
                if err: raise RuntimeError('%s -> %s' % (line, err))
                return out
            if ln.startswith('ERR'): err = ln; continue          # the firmware still sends END after an error: drain it so replies stay in step
            out.append(ln)
        raise TimeoutError('no END after %ds for %r; got %r' % (timeout, line, out[-3:]))


def kv(line):
    """'R case=x c=3 mean_us=1.5' -> dict of the key=value tokens (numbers converted)."""
    d = {'tag': line.split()[0]}
    for tok in line.split():
        if '=' in tok:
            k, v = tok.split('=', 1)
            try: d[k] = int(v)
            except ValueError:
                try: d[k] = float(v)
                except ValueError: d[k] = v
    return d
