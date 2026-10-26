#!/usr/bin/env python3
"""Bounded, private original-game observations from an existing Dolphin GDB stub.

No emulator launch, guest memory/register writes, monitor commands, or game logic.
All candidate names describe source locations, not validated battle boundaries.
"""
import argparse
import ctypes
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import socket
import struct
import sys
import tempfile
import time

MEM1_START, MEM1_END = 0x80000000, 0x81800000
CHUNK = 256
MAX_READ = 4096
MAX_PACKET = 16384
RNG_POINTER = 0x80478C94
ATTEST_POINTER = 0x8047ADB8
SOURCE_SHA256 = 'dbc772fd356f490cab2a6d8ed6ec77cbcd35f647d01e0b0b06542cbd750abae1'
CANDIDATES = {
    'opponentTrainerCreate': 0x801F9CBC,
    'postHeroPreRoster': 0x801F9F78,
    'floorStart': 0x801F17B0,
    'floorInit': 0x801F4860,
    'turnCount': 0x801F68C8,
    'oneTurnPost': 0x8020BBA4,
    'resultSetter': 0x801F3984,
    'partyCopyback': 0x801F86C0,
}
SPECIAL_REGISTERS = {'pc': 64, 'cr': 66, 'lr': 67, 'ctr': 68, 'xer': 69}


class ProbeError(ValueError):
    pass


def number(value):
    if isinstance(value, bool):
        raise ProbeError('boolean is not an address/count')
    if isinstance(value, int):
        return value
    if isinstance(value, str) and re.fullmatch(r'(?:0x[0-9a-fA-F]+|[0-9]+)', value):
        return int(value, 16 if value.startswith('0x') else 10)
    raise ProbeError('invalid address/count')


def mem1(address, size):
    if not isinstance(address, int) or not isinstance(size, int) or not 0 < size <= MAX_READ:
        raise ProbeError('read size outside bounded MEM1 policy')
    if not MEM1_START <= address < address + size <= MEM1_END:
        raise ProbeError('address outside MEM1')
    return address


def instruction_address(address):
    mem1(address, 4)
    if address % 4:
        raise ProbeError('unaligned instruction address')
    return address


def tcp_endpoint(text):
    # Numeric addresses only: no DNS resolution or host aliases.
    try:
        host, port_text = text.rsplit(':', 1)
        host = host.removeprefix('[').removesuffix(']')
        address = ipaddress.ip_address(host)
        port = int(port_text)
    except ValueError as exc:
        raise ProbeError('expected numeric loopback IP:port') from exc
    if not address.is_loopback or not 1 <= port <= 65535:
        raise ProbeError('only loopback TCP endpoints are admitted')
    return str(address), port


def decode_payload(wire):
    result = bytearray()
    index = 0
    while index < len(wire):
        value = wire[index]
        if value == ord('}'):
            index += 1
            if index == len(wire):
                raise ProbeError('truncated packet escape')
            result.append(wire[index] ^ 0x20)
        elif value == ord('*'):
            index += 1
            if not result or index == len(wire):
                raise ProbeError('invalid packet RLE')
            count = wire[index] - 29
            if not 3 <= count <= 97 or wire[index] in b'$#+-':
                raise ProbeError('invalid packet RLE count')
            result.extend(result[-1:] * count)
        else:
            result.append(value)
        if len(result) > MAX_PACKET:
            raise ProbeError('expanded packet exceeds bound')
        index += 1
    return bytes(result)


def parse_stop(payload):
    """Accept only bare Sxx or the reviewed Dolphin SendSignal T field layout."""
    short = re.fullmatch(rb'S([0-9a-fA-F]{2})', payload)
    if short:
        return int(short[1], 16), None
    detailed = re.fullmatch(rb'T([0-9a-fA-F]{2})40:([0-9a-fA-F]{8});01:([0-9a-fA-F]{8});', payload)
    if not detailed:
        raise ProbeError('expected Dolphin stop signal with exact PC/SP fields')
    pc, sp = int(detailed[2], 16), int(detailed[3], 16)
    instruction_address(pc)
    mem1(sp, 4)
    if sp % 4:
        raise ProbeError('unaligned stop stack pointer')
    return int(detailed[1], 16), {'pc': pc, 'r1': sp}


class GDBClient:
    """ACK-mode RSP; any malformed packet closes the connection, without retry."""

    def __init__(self, connection, timeout=5, max_commands=512, max_seconds=60):
        if not 0 < timeout <= 30 or not 1 <= max_commands <= 2048 or not 1 <= max_seconds <= 3600:
            raise ProbeError('invalid control/time limits')
        self.connection = connection
        self.timeout = timeout
        self.deadline = time.monotonic() + max_seconds
        self.max_commands = max_commands
        self.commands = []
        self.breakpoints = set()
        self.stopped = False
        self.last_stop = None
        self.last_stop_signal = None
        self.closed = False

    def close(self):
        self.closed = True
        self.connection.close()

    def _byte(self, wait_for_stop=False):
        remaining = self.deadline - time.monotonic()
        if not wait_for_stop:
            remaining = min(self.timeout, remaining)
        if remaining <= 0:
            raise ProbeError('observation deadline reached')
        self.connection.settimeout(remaining)
        data = self.connection.recv(1)
        if not data:
            raise ProbeError('GDB connection ended')
        return data[0]

    def _packet(self, wait_for_stop=False):
        if self._byte(wait_for_stop) != ord('$'):
            raise ProbeError('expected reply packet')
        wire = bytearray()
        while True:
            value = self._byte()
            if value == ord('#'):
                break
            wire.append(value)
            if len(wire) > MAX_PACKET:
                raise ProbeError('wire packet exceeds bound')
        checksum = bytes((self._byte(), self._byte()))
        if not re.fullmatch(rb'[0-9a-fA-F]{2}', checksum) or int(checksum, 16) != sum(wire) % 256:
            raise ProbeError('invalid packet checksum')
        payload = decode_payload(wire)
        self.connection.sendall(b'+')
        return payload

    def _allow(self, command):
        if command in ('qSupported', '?', 'g', 'c', 's'):
            if command in ('c', 's') and not self.stopped:
                raise ProbeError('execution control requires a confirmed stop')
            return
        match = re.fullmatch(r'p([0-9a-f]{1,2})', command)
        if match and int(match[1], 16) in set(range(32)) | set(SPECIAL_REGISTERS.values()):
            return
        match = re.fullmatch(r'm([0-9a-f]{8}),([0-9a-f]+)', command)
        if match:
            mem1(int(match[1], 16), int(match[2], 16))
            if int(match[2], 16) <= CHUNK:
                return
        match = re.fullmatch(r'([Zz])0,([0-9a-f]{8}),4', command)
        if match and int(match[2], 16) in self.breakpoints:
            return
        raise ProbeError('command outside exact observation whitelist')

    def request(self, command):
        try:
            self._allow(command)
            if self.closed or len(self.commands) >= self.max_commands or time.monotonic() >= self.deadline:
                raise ProbeError('observation control cap reached')
            payload = command.encode('ascii')
            self.connection.settimeout(min(self.timeout, self.deadline - time.monotonic()))
            self.connection.sendall(b'$' + payload + b'#' + f'{sum(payload) % 256:02x}'.encode())
            self.commands.append(command)
            if self._byte() != ord('+'):
                raise ProbeError('missing ACK or negative ACK')
            if command in ('c', 's'):
                self.stopped = False
                self.last_stop = None
                self.last_stop_signal = None
            result = self._packet(wait_for_stop=command in ('c', 's'))
            if re.fullmatch(rb'E[0-9a-fA-F]{2}', result):
                raise ProbeError('GDB error reply: ' + result.decode('ascii', 'replace'))
            if command in ('?', 'c', 's'):
                signal, context = parse_stop(result)
                # The reviewed Dolphin ? handler reports SIGTERM while paused.
                if command == '?' and signal not in (5, 15):
                    raise ProbeError('unexpected initial stop signal')
                if command != '?' and signal != 5:
                    raise ProbeError('execution ended without breakpoint trap')
                self.last_stop, self.last_stop_signal = context, signal
                self.stopped = True
            return result
        except (OSError, ValueError, UnicodeError) as exc:
            self.close()
            if isinstance(exc, ProbeError):
                raise
            raise ProbeError('GDB transport/protocol failure') from exc

    def read_memory(self, address, size):
        mem1(address, size)
        result = bytearray()
        for offset in range(0, size, CHUNK):
            count = min(CHUNK, size - offset)
            payload = self.request(f'm{address + offset:08x},{count:x}')
            if len(payload) != count * 2 or not re.fullmatch(rb'[0-9a-fA-F]+', payload):
                self.close()
                raise ProbeError('invalid memory reply length/encoding')
            result.extend(bytes.fromhex(payload.decode()))
        return bytes(result)

    def registers(self):
        # Local Dolphin ReadRegisters returns ONLY 32 big-endian GPRs (256 hex digits).
        raw = self.request('g')
        if len(raw) != 256 or not re.fullmatch(rb'[0-9a-fA-F]{256}', raw):
            self.close()
            raise ProbeError('unsupported Dolphin GPR layout')
        result = {f'r{i}': int(raw[i * 8:i * 8 + 8], 16) for i in range(32)}
        for name, register in SPECIAL_REGISTERS.items():
            raw = self.request(f'p{register:x}')
            if not re.fullmatch(rb'[0-9a-fA-F]{8}', raw):
                self.close()
                raise ProbeError('unsupported Dolphin special-register layout')
            result[name] = int(raw, 16)
        if self.last_stop is not None and any(result[name] != value for name, value in self.last_stop.items()):
            self.close()
            raise ProbeError('register snapshot differs from stop reply PC/SP')
        return result


class OriginalDOL:
    def __init__(self, path, expected_sha256):
        if not re.fullmatch('[0-9a-f]{64}', expected_sha256):
            raise ProbeError('expected DOL SHA256 is required')
        if Path(path).stat().st_size > 64 * 1024 * 1024:
            raise ProbeError('DOL exceeds file bound')
        self.data = Path(path).read_bytes()
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        if self.sha256 != expected_sha256 or len(self.data) < 0x100:
            raise ProbeError('original DOL pin mismatch')
        self.sections = []
        for index in range(7):
            offset = struct.unpack_from('>I', self.data, index * 4)[0]
            address = struct.unpack_from('>I', self.data, 0x48 + index * 4)[0]
            size = struct.unpack_from('>I', self.data, 0x90 + index * 4)[0]
            if size:
                mem1(address, min(size, MAX_READ))
                if address + size > MEM1_END or offset < 0x100 or offset + size > len(self.data):
                    raise ProbeError('invalid DOL text section')
                self.sections.append((address, size, offset))

    def word(self, address):
        instruction_address(address)
        matches = [self.data[o + address - a:o + address - a + 4]
                   for a, n, o in self.sections if a <= address and address + 4 <= a + n]
        if len(matches) != 1:
            raise ProbeError('instruction not uniquely present in original DOL text')
        return matches[0]

    def verify(self, client, address):
        expected = self.word(address)
        if client.read_memory(address, 4) != expected:
            raise ProbeError('guest instruction differs from pinned original DOL')


def linked_target(address, word):
    instruction = int.from_bytes(word, 'big')
    if instruction >> 26 != 18 or instruction & 1 != 1:
        raise ProbeError('caller is not a direct linked branch')
    displacement = instruction & 0x03FFFFFC
    if displacement & 0x02000000:
        displacement -= 0x04000000
    return (displacement if instruction & 2 else address + displacement) & 0xFFFFFFFF


def validate_source(path):
    if Path(path).stat().st_size > 1024 * 1024:
        raise ProbeError('GDB source exceeds file bound')
    data = Path(path).read_bytes()
    # Normalize checkout line endings; exact reviewed source is required.
    digest = hashlib.sha256(data.replace(b'\r\n', b'\n')).hexdigest()
    if digest != SOURCE_SHA256:
        raise ProbeError('GDBStub.cpp differs from reviewed register/protocol source')
    return digest


def validate_plan(plan, dol):
    if not isinstance(plan, dict) or set(plan) - {'schema', 'probes', 'max_stops', 'max_seconds'}:
        raise ProbeError('invalid plan keys')
    if type(plan.get('schema')) is not int or plan['schema'] != 1 or not isinstance(plan.get('probes'), list) or not 1 <= len(plan['probes']) <= 8:
        raise ProbeError('invalid plan schema/probe count')
    stops = number(plan.get('max_stops', 16))
    seconds = number(plan.get('max_seconds', 60))
    if not 1 <= stops <= 32 or not 1 <= seconds <= 3600:
        raise ProbeError('invalid plan control caps')
    result = []
    for item in plan['probes']:
        if not isinstance(item, dict) or set(item) - {'name', 'capture_return', 'caller_addresses', 'objects'}:
            raise ProbeError('invalid probe keys')
        name = item.get('name')
        if not isinstance(name, str) or name not in CANDIDATES or type(item.get('capture_return', False)) is not bool:
            raise ProbeError('unknown candidate or return flag')
        entry = CANDIDATES[name]
        dol.word(entry)
        callers = item.get('caller_addresses', [])
        if not isinstance(callers, list) or len(callers) > 16:
            raise ProbeError('invalid caller list')
        callers = [instruction_address(number(value)) for value in callers]
        for caller in callers:
            if linked_target(caller, dol.word(caller)) != entry:
                raise ProbeError('pinned caller does not directly call candidate')
            dol.word(caller + 4)
        capture_return = item.get('capture_return', False)
        if capture_return and not callers:
            raise ProbeError('return capture requires pinned direct caller context')
        objects = item.get('objects', [])
        if not isinstance(objects, list) or len(objects) > 8:
            raise ProbeError('invalid object list')
        for obj in objects:
            if not isinstance(obj, dict) or set(obj) != {'name', 'register', 'size'}:
                raise ProbeError('invalid object keys')
            if not isinstance(obj['name'], str) or not re.fullmatch(r'[a-zA-Z][a-zA-Z0-9_]{0,47}', obj['name']):
                raise ProbeError('invalid object name')
            if obj['register'] not in [f'r{i}' for i in range(32)] or number(obj['size']) != 312:
                raise ProbeError('only register-derived 312-byte Pokemon objects admitted')
        result.append({'name': name, 'entry': entry, 'capture_return': capture_return,
                       'callers': callers, 'objects': objects})
    if sum(1 + int(item['capture_return']) for item in result) > stops:
        raise ProbeError('requested stops exceed plan cap')
    return result, stops, seconds


def snapshot(client, registers, objects, candidate=None):
    pointer = int.from_bytes(client.read_memory(RNG_POINTER, 4), 'big')
    mem1(pointer, 4)
    seed = int.from_bytes(client.read_memory(pointer, 4), 'big')
    rows = []
    for obj in objects:
        address = obj['address']
        mem1(address, 312)
        rows.append({'name': obj['name'], 'register': obj['register'], 'address': f'0x{address:08x}',
                     'size': 312, 'data_hex': client.read_memory(address, 312).hex()})
    result = {'registers': {key: f'0x{value:08x}' for key, value in registers.items()},
              'rng': {'pointer_address': f'0x{RNG_POINTER:08x}', 'seed_address': f'0x{pointer:08x}',
                      'seed': f'0x{seed:08x}'}, 'objects': rows}
    if candidate == 'postHeroPreRoster':
        # Independently sourced savedata status pointer, never inferred from Pokemon output.
        attest_pointer = int.from_bytes(client.read_memory(ATTEST_POINTER, 4), 'big')
        mem1(attest_pointer, 4)
        result['attest'] = {'pointer_address': f'0x{ATTEST_POINTER:08x}',
                            'pointee_address': f'0x{attest_pointer:08x}', 'offset': 0,
                            'data_hex': client.read_memory(attest_pointer, 4).hex()}
    return result


def breakpoint(client, dol, address, add):
    dol.verify(client, address)
    client.breakpoints.add(address)
    if client.request(f'{"Z" if add else "z"}0,{address:08x},4') != b'OK':
        raise ProbeError('breakpoint request rejected')
    if not add:
        client.breakpoints.remove(address)


def observe(client, dol, probes, max_stops):
    supported = client.request('qSupported').decode('ascii')
    if 'swbreak+' not in supported.split(';'):
        raise ProbeError('Dolphin software breakpoint capability missing')
    client.request('?')
    records = []
    stops = 0
    for index, probe in enumerate(probes):
        entry = probe['entry']
        breakpoint(client, dol, entry, True)
        client.request('c')
        stops += 1
        if stops > max_stops:
            raise ProbeError('stop cap reached')
        registers = client.registers()
        if registers['pc'] != entry:
            raise ProbeError('unexpected stop PC; candidate not observed')
        dol.verify(client, entry)
        objects = [dict(obj, address=mem1(registers[obj['register']], 312)) for obj in probe['objects']]
        record = {'capture_id': f'{index:02d}-{probe["name"]}',
                  'candidate': probe['name'], 'boundary_status': 'candidate_until_caller_context_validated',
                  'entry_address': f'0x{entry:08x}',
                  'entry': snapshot(client, registers, objects, probe['name'])}
        breakpoint(client, dol, entry, False)
        if probe['capture_return']:
            return_address = registers['lr']
            instruction_address(return_address)
            caller = return_address - 4
            if caller not in probe['callers']:
                raise ProbeError('observed LR does not match pinned source caller')
            dol.verify(client, caller)
            breakpoint(client, dol, return_address, True)
            client.request('c')
            stops += 1
            if stops > max_stops:
                raise ProbeError('stop cap reached')
            returned = client.registers()
            if returned['pc'] != return_address or returned['r1'] != registers['r1']:
                raise ProbeError('unexpected return PC/stack; return context unproven')
            record['caller_address'] = f'0x{caller:08x}'
            record['return'] = snapshot(client, returned, objects, probe['name'])
            breakpoint(client, dol, return_address, False)
        records.append(record)
    return {'schema': 1, 'kind': 'private_original_game_candidate_observation',
            'full_battle_qualified': False, 'dol_sha256': dol.sha256,
            'gdb_source_sha256': SOURCE_SHA256, 'register_layout': 'Dolphin g=32x32be; p40/42/43/44/45',
            'stops': stops, 'records': records, 'commands': client.commands,
            'completion_state': 'paused_at_last_observed_boundary'}


def private_output(output, private_root):
    output, root = Path(output).resolve(), Path(private_root).resolve()
    repository = Path(__file__).resolve().parents[2]
    if not root.is_dir() or not output.is_relative_to(root) or output == root:
        raise ProbeError('output must be a new child directory inside existing private root')
    if output.is_relative_to(repository) or root.is_relative_to(repository):
        raise ProbeError('private output must be outside public checkout')
    if any((parent / '.git').exists() for parent in (output, *output.parents)):
        raise ProbeError('raw guest observations cannot be published inside a Git checkout')
    if output.exists() or not output.parent.is_dir():
        raise ProbeError('refusing existing output or missing parent')
    return output


def publish_new_directory(stage, output):
    """Atomic no-replace publication; ordinary POSIX rename can replace an empty dir."""
    if os.name == 'nt':
        os.rename(stage, output)  # Windows rename refuses any existing destination.
    elif sys.platform.startswith('linux'):
        library = ctypes.CDLL(None, use_errno=True)
        rename = getattr(library, 'renameat2', None)
        if rename is None:
            raise ProbeError('atomic no-replace directory publication unavailable')
        rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        rename.restype = ctypes.c_int
        if rename(-100, os.fsencode(stage), -100, os.fsencode(output), 1) != 0:
            error = ctypes.get_errno()
            raise OSError(error, os.strerror(error), str(output))
    else:
        raise ProbeError('atomic no-replace directory publication unsupported on this platform')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    endpoint = parser.add_mutually_exclusive_group(required=True)
    endpoint.add_argument('--tcp', type=tcp_endpoint)
    endpoint.add_argument('--unix', type=Path)
    parser.add_argument('--dol', required=True, type=Path)
    parser.add_argument('--dol-sha256', required=True)
    parser.add_argument('--gdb-source', required=True, type=Path)
    parser.add_argument('--plan', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--private-root', required=True, type=Path)
    parser.add_argument('--isolation-evidence', type=Path)
    parser.add_argument('--timeout', type=float, default=5)
    args = parser.parse_args(argv)
    connection = None
    try:
        output = private_output(args.output, args.private_root)
        validate_source(args.gdb_source)
        dol = OriginalDOL(args.dol, args.dol_sha256)
        if args.plan.stat().st_size > 65536:
            raise ProbeError('plan exceeds file bound')
        probes, stops, seconds = validate_plan(json.loads(args.plan.read_text()), dol)
        evidence_sha256 = None
        if args.tcp:
            if args.isolation_evidence is None or not args.isolation_evidence.is_file() or not args.isolation_evidence.stat().st_size:
                raise ProbeError('TCP requires operator-reviewed launch isolation evidence (server binds INADDR_ANY)')
            evidence_sha256 = hashlib.sha256(args.isolation_evidence.read_bytes()).hexdigest()
        if not 0 < args.timeout <= 30:
            raise ProbeError('timeout must be in (0,30] seconds')
        if args.tcp:
            connection = socket.create_connection(args.tcp, timeout=args.timeout)
            if not ipaddress.ip_address(connection.getpeername()[0]).is_loopback:
                raise ProbeError('connected peer is not loopback')
        else:
            if not args.unix.is_absolute():
                raise ProbeError('Unix socket path must be absolute')
            connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            connection.settimeout(args.timeout)
            connection.connect(str(args.unix))
        client = GDBClient(connection, timeout=args.timeout, max_seconds=seconds)
        capture = observe(client, dol, probes, stops)
        capture['transport'] = 'loopback_tcp' if args.tcp else 'local_unix_socket'
        capture['isolation_evidence_sha256'] = evidence_sha256
        client.close()
        # Stage complete output privately, then rename as one directory; never overwrite.
        with tempfile.TemporaryDirectory(prefix='.gdb-probe-', dir=output.parent) as temporary:
            stage = Path(temporary) / 'capture'
            stage.mkdir(mode=0o700)
            (stage / 'observation.json').write_text(json.dumps(capture, indent=2) + '\n')
            for record in capture['records']:
                (stage / (record['capture_id'] + '.json')).write_text(json.dumps(record, indent=2) + '\n')
            if output.exists():
                raise ProbeError('refusing output that appeared during capture')
            publish_new_directory(stage, output)
        print(json.dumps({'status': 'captured_candidate_observation', 'output': str(output), 'full_battle_qualified': False}))
        return 0
    except (OSError, ValueError, TypeError, KeyError) as exc:
        print('probe failed closed: ' + str(exc), file=sys.stderr)
        return 1
    finally:
        if connection is not None:
            connection.close()


if __name__ == '__main__':
    raise SystemExit(main())
