"""Local fake RSP transport and bounded original-game observation policy tests."""
import copy
import hashlib
import json
from pathlib import Path
import socket
import struct
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/qualification'))
import dolphin_gdb_probe as PROBE


def packet(wire):
    return b'$' + wire + b'#' + f'{sum(wire) % 256:02x}'.encode()


class FakeServer:
    """A real local socket peer, never an emulator or nonlocal endpoint."""
    def __init__(self, replies, fragment=False):
        self.client, self.server = socket.socketpair()
        self.server.settimeout(2)
        self.replies = replies
        self.fragment = fragment
        self.commands = []
        self.error = None
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        try:
            for reply in self.replies:
                command = bytearray()
                while True:
                    byte = self.server.recv(1)
                    if not byte:
                        return
                    command.extend(byte)
                    if len(command) >= 4 and command[-3:-2] == b'#':
                        break
                self.commands.append(bytes(command))
                if reply is None:
                    return
                if isinstance(reply, float):
                    time.sleep(reply)
                    return
                sent_ack = isinstance(reply, tuple)
                if sent_ack:
                    self.server.sendall(b'+')
                    time.sleep(reply[0])
                    reply = reply[1]
                response = reply if sent_ack or reply.startswith(b'-') else b'+' + reply
                if self.fragment:
                    for byte in response:
                        self.server.sendall(bytes([byte]))
                else:
                    self.server.sendall(response)
                if not response.startswith(b'-') and b'#' in response:
                    # Successful packets are acknowledged; corrupt ones close without ACK.
                    ack = self.server.recv(1)
                    if ack and ack != b'+':
                        raise AssertionError('missing client packet ACK')
        except OSError:
            pass  # Fail-closed client may close while a malformed reply is in flight.
        except BaseException as exc:
            self.error = exc
        finally:
            self.server.close()

    def finish(self):
        self.client.close()
        self.thread.join(3)
        if self.error:
            raise self.error
        if self.thread.is_alive():
            raise AssertionError('fake peer did not terminate')


class ProtocolTests(unittest.TestCase):
    def test_exact_live_initial_dolphin_stop_and_trap_context(self):
        initial = b'T0f40:80003154;01:81566550;'
        trap = b'T0540:801f9f78;01:81566550;'
        server = FakeServer([packet(initial), packet(trap)], True)
        client = PROBE.GDBClient(server.client)
        self.assertEqual(client.request('?'), initial)
        self.assertEqual(client.last_stop, {'pc': 0x80003154, 'r1': 0x81566550})
        self.assertEqual(client.last_stop_signal, 15)
        self.assertEqual(client.request('c'), trap)
        self.assertEqual(client.last_stop, {'pc': 0x801F9F78, 'r1': 0x81566550})
        self.assertEqual(client.last_stop_signal, 5)
        client.close()
        server.finish()

    def test_detailed_stop_rejects_unknown_duplicate_missing_and_invalid_fields(self):
        malformed = [
            b'T0f40:80003154;', b'T0f01:81566550;',
            b'T0f40:80003154;01:81566550;01:81566550;',
            b'T0f40:80003154;40:80003154;01:81566550;',
            b'T0f40:80003154;01:81566550;thread:1;',
            b'T0f41:80003154;01:81566550;',
            b'T0f01:81566550;40:80003154;',
            b'T0f40:80003154;1:81566550;',
            b'T0f40:8000315;01:81566550;',
            b'T0f40:80003154;01:8156655g;',
            b'T0f40:00003154;01:81566550;',
            b'T0f40:81800000;01:81566550;',
            b'T0f40:80003155;01:81566550;',
            b'T0f40:80003154;01:81800000;',
            b'T0f40:80003154;01:7ffffffc;',
            b'T0f40:80003154;01:81566551;',
            b'T1040:80003154;01:81566550;',
        ]
        for reply in malformed:
            with self.subTest(reply=reply):
                server = FakeServer([packet(reply)], True)
                client = PROBE.GDBClient(server.client)
                with self.assertRaises(PROBE.ProbeError):
                    client.request('?')
                self.assertTrue(client.closed)
                server.finish()
        for signal in (b'0f', b'06'):
            server = FakeServer([packet(b'T0f40:80003154;01:81566550;'),
                                 packet(b'T' + signal + b'40:801f9f78;01:81566550;')])
            client = PROBE.GDBClient(server.client)
            client.request('?')
            with self.assertRaisesRegex(PROBE.ProbeError, 'without breakpoint trap'):
                client.request('c')
            server.finish()

    def test_register_snapshots_crosscheck_detailed_stop_pc_and_stack(self):
        pc, stack = 0x801F9F78, 0x81566550
        for mismatch in (None, 'pc', 'r1'):
            with self.subTest(mismatch=mismatch):
                gpr = [0] * 32
                gpr[1] = stack + (4 if mismatch == 'r1' else 0)
                registers = b''.join(f'{value:08x}'.encode() for value in gpr)
                values = (pc + (4 if mismatch == 'pc' else 0), 0, 0x801F9D08, 0, 0)
                replies = [packet(b'T0540:801f9f78;01:81566550;'), packet(registers)]
                replies += [packet(f'{value:08x}'.encode()) for value in values]
                server = FakeServer(replies, True)
                client = PROBE.GDBClient(server.client)
                client.request('?')
                if mismatch is None:
                    self.assertEqual(client.registers()['pc'], pc)
                    client.close()
                else:
                    with self.assertRaisesRegex(PROBE.ProbeError, 'differs from stop reply'):
                        client.registers()
                    self.assertTrue(client.closed)
                server.finish()

    def test_fragmented_checksums_ack_escape_and_rle(self):
        # Escaped '#' and literal '*' plus four A bytes using RLE.
        server = FakeServer([packet(b'hello}'+ bytes([ord('#') ^ 0x20]) + b'}' + bytes([ord('*') ^ 0x20]) + b'A* ')], True)
        client = PROBE.GDBClient(server.client)
        self.assertEqual(client.request('qSupported'), b'hello#*AAAA')
        self.assertEqual(server.commands[0], packet(b'qSupported'))
        client.close()
        server.finish()

    def test_bad_checksum_nack_eof_error_and_malformed_payload_fail_closed(self):
        for reply in (b'$data#00', b'-', None, packet(b'E01'), packet(b'a}'), packet(b'* '), packet(b'A*\x1d')):
            with self.subTest(reply=reply):
                server = FakeServer([reply], True)
                client = PROBE.GDBClient(server.client)
                with self.assertRaises(PROBE.ProbeError):
                    client.request('qSupported')
                self.assertTrue(client.closed)
                server.finish()

    def test_timeout_and_packet_expansion_bounds(self):
        server = FakeServer([0.15])
        client = PROBE.GDBClient(server.client, timeout=0.02)
        with self.assertRaises(PROBE.ProbeError):
            client.request('qSupported')
        self.assertTrue(client.closed)
        server.finish()
        with self.assertRaises(PROBE.ProbeError):
            PROBE.decode_payload(b'A' + b'*~' * 200)
        server = FakeServer([packet(b'a' * (PROBE.MAX_PACKET + 1))])
        client = PROBE.GDBClient(server.client)
        with self.assertRaises(PROBE.ProbeError):
            client.request('qSupported')
        server.finish()

    def test_forbidden_commands_never_reach_peer(self):
        forbidden = ['M80000000,4:12345678', 'X80000000,4:abcd', 'G00000000', 'P40=80000000',
                     'qRcmd,68656c70', 'qSupported:xmlRegisters=powerpc', 'vRun', 'vCont;c',
                     'c80000000', 's80000000', 'C05', 'k', 'D', '\x03', 'Hc0',
                     'Z1,801f9cbc,4', 'Z0,801f9cbc,4', 'z0,801f9cbc,4',
                     'm81800000,4', 'm80000000,101', 'p20', 'pff', '?extra']
        for command in forbidden:
            with self.subTest(command=command):
                left, right = socket.socketpair()
                client = PROBE.GDBClient(left)
                with self.assertRaises(PROBE.ProbeError):
                    client.request(command)
                self.assertEqual(right.recv(1), b'')
                right.close()

    def test_stopped_control_and_command_caps(self):
        left, right = socket.socketpair()
        client = PROBE.GDBClient(left)
        with self.assertRaises(PROBE.ProbeError):
            client.request('c')
        right.close()
        server = FakeServer([packet(b'S0f')])
        client = PROBE.GDBClient(server.client, max_commands=1)
        self.assertEqual(client.request('?'), b'S0f')
        with self.assertRaisesRegex(PROBE.ProbeError, 'cap'):
            client.request('g')
        server.finish()

    def test_continue_waits_for_stop_within_overall_budget(self):
        server = FakeServer([packet(b'S0f'), (0.08, packet(b'S05'))])
        client = PROBE.GDBClient(server.client, timeout=0.02, max_seconds=1)
        client.request('?')
        self.assertEqual(client.request('c'), b'S05')
        client.close()
        server.finish()
        server = FakeServer([packet(b'S0f'), packet(b'W00')])
        client = PROBE.GDBClient(server.client)
        client.request('?')
        with self.assertRaisesRegex(PROBE.ProbeError, 'stop signal'):
            client.request('c')
        server.finish()

    def test_source_specific_register_layout_and_memory_chunks(self):
        gpr = b''.join(f'{index:08x}'.encode() for index in range(32))
        server = FakeServer([packet(gpr)] + [packet(f'{index:08x}'.encode()) for index in (64, 66, 67, 68, 69)])
        client = PROBE.GDBClient(server.client)
        registers = client.registers()
        self.assertEqual(registers['r31'], 31)
        self.assertEqual(registers['pc'], 64)
        self.assertEqual(registers['lr'], 67)
        self.assertEqual([cmd.split(b'#')[0][1:] for cmd in server.commands],
                         [b'g', b'p40', b'p42', b'p43', b'p44', b'p45'])
        client.close()
        server.finish()
        # Dolphin emits uppercase memory hex; a leading E is data, not E01.
        server = FakeServer([packet(b'EE' * 256), packet(b'BB' * 56)])
        client = PROBE.GDBClient(server.client)
        self.assertEqual(client.read_memory(0x80000000, 312), b'\xee' * 256 + b'\xbb' * 56)
        client.close()
        server.finish()
        for data in (b'00' * 31, b'x' * 256):
            server = FakeServer([packet(data)])
            client = PROBE.GDBClient(server.client)
            with self.assertRaises(PROBE.ProbeError):
                client.registers()
            server.finish()
        server = FakeServer([packet(b'zz')])
        client = PROBE.GDBClient(server.client)
        with self.assertRaises(PROBE.ProbeError):
            client.read_memory(0x80000000, 1)
        server.finish()


def synthetic_dol(directory, name='opponentTrainerCreate'):
    base = PROBE.CANDIDATES[name]
    data = bytearray(0x300)
    struct.pack_into('>I', data, 0, 0x100)
    struct.pack_into('>I', data, 0x48, base - 0x10)
    struct.pack_into('>I', data, 0x90, 0x200)
    struct.pack_into('>I', data, 0x100, (18 << 26) | 0x10 | 1)
    struct.pack_into('>I', data, 0x110, (37 << 26) | (1 << 21) | (1 << 16) | (0x10000 - 0x150))
    if name == 'opponentTrainerCreate':
        argument_offset = 0x100 + PROBE.PARENT_SCRATCH_ARGUMENT - (base - 0x10)
        struct.pack_into('>I', data, argument_offset, (14 << 26) | (4 << 21) | (1 << 16) | 8)
    path = Path(directory) / 'synthetic.dol'
    path.write_bytes(data)
    return PROBE.OriginalDOL(path, hashlib.sha256(data).hexdigest())


class MemoryClient:
    def __init__(self, dol, entry, pointer=0x80400000):
        self.dol = dol
        self.entry = entry
        self.pointer = pointer
        self.commands = []
        self.breakpoints = set()
        self.current = entry
        self.stopped = True
        self.reads = []

    def read_memory(self, address, size):
        self.reads.append((address, size))
        if address == PROBE.RNG_POINTER:
            return self.pointer.to_bytes(4, 'big')
        if address == self.pointer:
            return b'\x12\x34\x56\x78'
        if address == PROBE.ATTEST_POINTER:
            return bytes.fromhex('80410000')
        if address == 0x80410000:
            return bytes.fromhex('01020304')
        if address == 0x80410000 + PROBE.SAVED_HERO_OFFSET and size == PROBE.HERO_SIZE:
            return b'\x11' * PROBE.HERO_SIZE
        if address == PROBE.TRAINER_SELECTOR:
            return bytes.fromhex('00000099')
        if size == 312:
            return b'\x00' * 312
        return self.dol.word(address)

    def request(self, command):
        self.commands.append(command)
        if command == 'qSupported':
            return b'swbreak+;hwbreak+'
        if command == '?':
            return b'S0f'
        if command == 'c':
            self.current = next(iter(self.breakpoints))
            return b'S05'
        return b'OK'

    def registers(self):
        return {**{f'r{i}': 0 for i in range(32)}, 'pc': self.current,
                'lr': self.entry - 0xC, 'cr': 0, 'ctr': 0, 'xer': 0,
                'r1': 0x81700000, 'r4': 0x80400100}


class ObservationPolicyTests(unittest.TestCase):
    def test_fight_pokemon_exact_candidate_object_policy(self):
        with tempfile.TemporaryDirectory() as directory:
            for candidate in ('fightPokemonCreate', 'opponentTrainerCreate', 'postHeroPreRoster'):
                dol = synthetic_dol(directory, candidate)
                for name, register, size in (('fight_pokemon', 'r3', 340), ('fight_pokemon', 'r4', 340),
                                             ('fight_pokemon', 'r3', 339), ('fight_pokemon', 'r3', 341),
                                             ('fight_pokemon', 'r3', 312), ('pokemon', 'r4', 312),
                                             ('pokemon', 'r3', 340), ('pokemon', 'r3', 312)):
                    admitted = ((name, register, size) in (('fight_pokemon', 'r3', 340), ('pokemon', 'r4', 312))
                                if candidate == 'fightPokemonCreate' else name == 'pokemon' and size == 312)
                    plan = {'schema': 1, 'probes': [{'name': candidate, 'objects': [
                        {'name': name, 'register': register, 'size': size}]}]}
                    with self.subTest(candidate=candidate, name=name, register=register, size=size):
                        if admitted:
                            PROBE.validate_plan(plan, dol)
                        else:
                            with self.assertRaises(PROBE.ProbeError):
                                PROBE.validate_plan(plan, dol)

    def test_fight_pokemon_retains_destination_source_and_observes_rng(self):
        with tempfile.TemporaryDirectory() as directory:
            dol = synthetic_dol(directory, 'fightPokemonCreate')
            entry = PROBE.CANDIDATES['fightPokemonCreate']
            plan = {'schema': 1, 'probes': [{'name': 'fightPokemonCreate', 'capture_return': True,
                    'caller_addresses': [hex(entry - 0x10)], 'objects': [
                        {'name': 'fight_pokemon', 'register': 'r3', 'size': 340},
                        {'name': 'pokemon', 'register': 'r4', 'size': 312}]}]}
            probes, stops, _ = PROBE.validate_plan(plan, dol)
            class ConstructorMemory(MemoryClient):
                change_seed = False
                bad_range = False
                bad_source_range = False

                def registers(self):
                    registers = super().registers()
                    returned = self.current != self.entry
                    registers['r3'] = 0x80422000 if returned else (0x817FFEB0 if self.bad_range else 0x80420000)
                    registers['r4'] = 0x80423000 if returned else (0x817FFEC9 if self.bad_source_range else 0x80421000)
                    return registers

                def read_memory(self, address, size):
                    if address == 0x80420000 and size == 340:
                        self.reads.append((address, size))
                        return (b'\xa1' if self.current == self.entry else b'\xb2') * 340
                    if address == 0x80421000 and size == 312:
                        self.reads.append((address, size))
                        return b'\xc3' * 312
                    if address == self.pointer and self.change_seed and self.current != self.entry:
                        self.reads.append((address, size))
                        return bytes.fromhex('12345679')
                    return super().read_memory(address, size)
            for changed in (False, True):
                client = ConstructorMemory(dol, entry)
                client.change_seed = changed
                record = PROBE.observe(client, dol, probes, stops)['records'][0]
                self.assertEqual(record['entry']['objects'][0]['data_hex'], (b'\xa1' * 340).hex())
                self.assertEqual(record['return']['objects'][0]['data_hex'], (b'\xb2' * 340).hex())
                self.assertEqual(record['entry']['objects'][1]['data_hex'], (b'\xc3' * 312).hex())
                self.assertEqual(record['return']['objects'][1]['data_hex'], (b'\xc3' * 312).hex())
                self.assertEqual(client.reads.count((0x80420000, 340)), 2)
                self.assertEqual(client.reads.count((0x80421000, 312)), 2)
                self.assertFalse(any(address in (0x80422000, 0x80423000) for address, _ in client.reads))
                self.assertEqual(record['rng_observation'], {'seed_unchanged': not changed, 'seed_address_unchanged': True})
                self.assertNotIn('parent_entry_inputs', record['entry'])
                self.assertNotIn('attest', record['entry'])
            invalid = ConstructorMemory(dol, entry)
            invalid.bad_range = True
            with self.assertRaises(PROBE.ProbeError):
                PROBE.observe(invalid, dol, probes, stops)
            self.assertFalse(any(size == 340 for _, size in invalid.reads))
            invalid_source = ConstructorMemory(dol, entry)
            invalid_source.bad_source_range = True
            with self.assertRaises(PROBE.ProbeError):
                PROBE.observe(invalid_source, dol, probes, stops)
            self.assertFalse(any(size in (312, 340) for _, size in invalid_source.reads))
            wrong_caller = copy.deepcopy(plan)
            wrong_caller['probes'][0]['caller_addresses'] = [hex(entry)]
            with self.assertRaisesRegex(PROBE.ProbeError, 'direct linked branch'):
                PROBE.validate_plan(wrong_caller, dol)
            class ConstructorWrongPin(ConstructorMemory):
                def read_memory(self, address, size):
                    if address == entry - 0x10:
                        return b'\x00' * 4
                    return super().read_memory(address, size)
            with self.assertRaisesRegex(PROBE.ProbeError, 'instruction differs'):
                PROBE.observe(ConstructorWrongPin(dol, entry), dol, probes, stops)
            class ConstructorWrongEntryPin(ConstructorMemory):
                def read_memory(self, address, size):
                    if address == entry:
                        return b'\x00' * 4
                    return super().read_memory(address, size)
            wrong_entry = ConstructorWrongEntryPin(dol, entry)
            with self.assertRaisesRegex(PROBE.ProbeError, 'instruction differs'):
                PROBE.observe(wrong_entry, dol, probes, stops)
            self.assertNotIn('c', wrong_entry.commands)

    def test_parent_return_scratch_reads_retained_entry_address_and_changed_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            dol = synthetic_dol(directory)
            entry = PROBE.CANDIDATES['opponentTrainerCreate']
            plan = {'schema': 1, 'probes': [{'name': 'opponentTrainerCreate', 'capture_return': True,
                    'caller_addresses': [hex(entry - 0x10)]}]}
            probes, stops, _ = PROBE.validate_plan(plan, dol)
            class ScratchMemory(MemoryClient):
                def registers(self):
                    registers = super().registers()
                    # These clobbered return registers must never supply scratch addresses.
                    registers['r3'], registers['r5'] = 0x81701000, 0x81702000
                    return registers

                def read_memory(self, address, size):
                    if address == 0x81700000 - 0x148 and size == 312:
                        self.reads.append((address, size))
                        return (b'\x15' if self.current == self.entry else b'\xa3') * 312
                    return super().read_memory(address, size)
            client = ScratchMemory(dol, entry)
            record = PROBE.observe(client, dol, probes, stops)['records'][0]
            initial = record['entry']['parent_entry_inputs']['pokemon_scratch']
            final = record['return']['parent_return_scratch']
            self.assertEqual(final, {'address': initial['address'], 'size': 312, 'data_hex': (b'\xa3' * 312).hex()})
            self.assertEqual(initial['data_hex'], (b'\x15' * 312).hex())
            self.assertEqual(client.reads.count((0x816FFEB8, 312)), 2)
            original_inputs = PROBE.parent_entry_inputs
            def corrupt_range(*args):
                result = original_inputs(*args)
                result['pokemon_scratch']['address'] = '0x81800000'
                return result
            with mock.patch.object(PROBE, 'parent_entry_inputs', side_effect=corrupt_range):
                bad = ScratchMemory(dol, entry)
                with self.assertRaises(PROBE.ProbeError):
                    PROBE.observe(bad, dol, probes, stops)
                self.assertEqual(bad.commands.count('c'), 1)
            class WrongCallerPin(ScratchMemory):
                def read_memory(self, address, size):
                    if address == entry - 0x10:
                        return b'\x00' * 4
                    return super().read_memory(address, size)
            with self.assertRaisesRegex(PROBE.ProbeError, 'instruction differs'):
                PROBE.observe(WrongCallerPin(dol, entry), dol, probes, stops)

    def test_parent_scratch_exact_source_semantics_and_guest_argument_pin(self):
        with tempfile.TemporaryDirectory() as directory:
            dol = synthetic_dol(directory)
            PROBE.validate_parent_scratch_source(dol)
            entry = PROBE.CANDIDATES['opponentTrainerCreate']
            class AlteredSource:
                def word(self, address):
                    word = int.from_bytes(dol.word(address), 'big')
                    return (word ^ bit if address == altered_address else word).to_bytes(4, 'big')
            for altered_address in (entry, PROBE.PARENT_SCRATCH_ARGUMENT):
                for bit in (1 << 26, 1 << 21, 1 << 16, 1):
                    with self.subTest(address=altered_address, bit=bit), self.assertRaises(PROBE.ProbeError):
                        PROBE.validate_parent_scratch_source(AlteredSource())
            plan = {'schema': 1, 'probes': [{'name': 'opponentTrainerCreate'}]}
            probes, stops, _ = PROBE.validate_plan(plan, dol)
            class WrongGuestArgument(MemoryClient):
                def read_memory(self, address, size):
                    if address == PROBE.PARENT_SCRATCH_ARGUMENT:
                        return b'\x00' * 4
                    return super().read_memory(address, size)
            with self.assertRaisesRegex(PROBE.ProbeError, 'instruction differs'):
                PROBE.observe(WrongGuestArgument(dol, entry), dol, probes, stops)

    def test_parent_fixed_entry_inputs_and_conditional_bounded_name(self):
        class ParentInputsMemory:
            def __init__(self, selector, name_pointer=0x80420000):
                self.selector = selector
                self.name_pointer = name_pointer
                self.stack = 0x81700000
                self.reads = []

            def read_memory(self, address, size):
                self.reads.append((address, size))
                if address == self.stack - 0x148 and size == 312:
                    return b'\xc3' * 312
                if address == 0x80410070 and size == PROBE.HERO_SIZE:
                    return b'\xa5' * PROBE.HERO_SIZE
                if address == PROBE.TRAINER_SELECTOR and size == 4:
                    return self.selector.to_bytes(4, 'big')
                if address == PROBE.REPLACEMENT_NAME_POINTER and size == 4:
                    return self.name_pointer.to_bytes(4, 'big')
                if address == 0x80420000 and size == 22:
                    return bytes.fromhex('004100420043004400450046004700480049004a004b')
                raise AssertionError('unexpected parent input read')
        for applies in (False, True):
            memory = ParentInputsMemory(17 if applies else 18)
            result = PROBE.parent_entry_inputs(memory, {'r3': 17, 'r1': 0x81700000}, 0x80410000)
            self.assertEqual(result['pokemon_scratch'], {'entry_stack': '0x81700000', 'offset': -0x148,
                             'address': '0x816ffeb8', 'size': 312, 'data_hex': (b'\xc3' * 312).hex()})
            self.assertEqual(result['saved_hero']['address'], '0x80410070')
            self.assertEqual(result['saved_hero']['size'], 0xB18)
            self.assertEqual(bytes.fromhex(result['saved_hero']['data_hex']), b'\xa5' * 0xB18)
            expected_reads = [(0x816FFEB8, 312), (0x80410070, 0xB18), (0x80478960, 4)]
            if applies:
                expected_reads += [(0x8047A670, 4), (0x80420000, 22)]
                self.assertEqual(result['replacement_name']['size'], 22)
            else:
                self.assertEqual(result['replacement_name'], {'applies': False})
            self.assertEqual(memory.reads, expected_reads)
        for pointer in (0, 0x81800000 - 21, 0x81800000, 0xFFFFFFFF):
            memory = ParentInputsMemory(17, pointer)
            with self.assertRaises(PROBE.ProbeError):
                PROBE.parent_entry_inputs(memory, {'r3': 17, 'r1': 0x81700000}, 0x80410000)
            self.assertEqual(memory.reads[-1], (PROBE.REPLACEMENT_NAME_POINTER, 4))
        memory = ParentInputsMemory(17)
        with self.assertRaises(PROBE.ProbeError):
            PROBE.parent_entry_inputs(memory, {'r3': 17, 'r1': 0x81700000}, PROBE.MEM1_END - 4)
        self.assertEqual(memory.reads, [])
        # Parent r1 decoded from failed attempt 08's g packet, not a child frame.
        # Live original-game stack is eight-mod-sixteen, not sixteen aligned.
        # The pinned stwu r1,-0x150(r1) preserves that incoming alignment.
        for stack, scratch in ((0x8053C8F8, 0x8053C7B0), (0x80000148, PROBE.MEM1_START)):
            memory = ParentInputsMemory(18)
            memory.stack = stack
            result = PROBE.parent_entry_inputs(memory, {'r3': 17, 'r1': stack}, 0x80410000)
            self.assertEqual(stack % 16, 8)
            self.assertEqual(result['pokemon_scratch']['address'], f'0x{scratch:08x}')
            self.assertEqual(result['pokemon_scratch']['size'], 312)
            self.assertEqual(len(bytes.fromhex(result['pokemon_scratch']['data_hex'])), 312)
            self.assertEqual(memory.reads[0], (scratch, 312))
        for stack in (0, 0x80000000, 0x80000140, 0x81800000, 0x81700004,
                      0x8053C8FC, 0x8000014C, 0xFFFFFFFF):
            memory = ParentInputsMemory(17)
            with self.subTest(stack=stack), self.assertRaises(PROBE.ProbeError):
                PROBE.parent_entry_inputs(memory, {'r3': 17, 'r1': stack}, 0x80410000)
            self.assertEqual(memory.reads, [])

    def test_exact_parent_hero_policy_and_other_candidates_remain_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            for candidate in ('opponentTrainerCreate', 'postHeroPreRoster'):
                dol = synthetic_dol(directory, candidate)
                for name, register, size in (('hero', 'r5', 0xB18), ('hero', 'r4', 0xB18),
                                             ('pokemon', 'r5', 0xB18), ('hero', 'r5', 0xB17),
                                             ('hero', 'r5', 0xB19), ('hero', 'r5', 4096),
                                             ('hero', 'r5', 312), ('pokemon', 'r4', 312)):
                    plan = {'schema': 1, 'probes': [{'name': candidate, 'objects': [
                            {'name': name, 'register': register, 'size': size}]}]}
                    admitted = (name, register, size) == ('pokemon', 'r4', 312) or (
                        candidate == 'opponentTrainerCreate' and (name, register, size) == ('hero', 'r5', 0xB18))
                    with self.subTest(candidate=candidate, name=name, register=register, size=size):
                        if admitted:
                            probes, _, _ = PROBE.validate_plan(plan, dol)
                            self.assertEqual(probes[0]['objects'][0]['size'], size)
                        else:
                            with self.assertRaises(PROBE.ProbeError):
                                PROBE.validate_plan(plan, dol)

    def test_parent_hero_exact_range_retains_entry_pointer_and_return_frame(self):
        with tempfile.TemporaryDirectory() as directory:
            dol = synthetic_dol(directory)
            entry = PROBE.CANDIDATES['opponentTrainerCreate']
            plan = {'schema': 1, 'probes': [{'name': 'opponentTrainerCreate', 'capture_return': True,
                    'caller_addresses': [hex(entry - 0x10)],
                    'objects': [{'name': 'hero', 'register': 'r5', 'size': '0xb18'}]}]}
            probes, stops, _ = PROBE.validate_plan(plan, dol)
            class HeroMemory(MemoryClient):
                hero_address = PROBE.MEM1_END - PROBE.HERO_SIZE
                wrong_return_stack = False

                def registers(self):
                    registers = super().registers()
                    registers['r5'] = self.hero_address if self.current == self.entry else 0
                    registers['r3'] = 17 if self.current == self.entry else 18
                    if self.current != self.entry and self.wrong_return_stack:
                        registers['r1'] += 4
                    return registers

                def read_memory(self, address, size):
                    if address in (PROBE.TRAINER_SELECTOR, PROBE.REPLACEMENT_NAME_POINTER, 0x80420000):
                        self.reads.append((address, size))
                        return {PROBE.TRAINER_SELECTOR: bytes.fromhex('00000011'),
                                PROBE.REPLACEMENT_NAME_POINTER: bytes.fromhex('80420000'),
                                0x80420000: b'\x00\x41' * 11}[address]
                    if size == PROBE.HERO_SIZE and address != 0x80410070:
                        self.reads.append((address, size))
                        if address != self.hero_address:
                            raise AssertionError('hero return read must retain entry pointer')
                        return bytes((index % 256 for index in range(size)))
                    return super().read_memory(address, size)
            client = HeroMemory(dol, entry)
            result = PROBE.observe(client, dol, probes, stops)
            record = result['records'][0]
            for boundary in ('entry', 'return'):
                obj = record[boundary]['objects'][0]
                self.assertEqual(obj['address'], f'0x{client.hero_address:08x}')
                self.assertEqual(obj['size'], 0xB18)
                self.assertEqual(len(bytes.fromhex(obj['data_hex'])), 0xB18)
                self.assertEqual(record[boundary]['attest']['data_hex'], '01020304')
            self.assertEqual(record['return']['registers']['r5'], '0x00000000')
            self.assertIn('parent_entry_inputs', record['entry'])
            self.assertNotIn('parent_entry_inputs', record['return'])
            self.assertTrue(record['entry']['parent_entry_inputs']['replacement_name']['applies'])
            self.assertEqual(record['return']['registers']['r3'], '0x00000012')
            self.assertEqual(client.reads.count((PROBE.TRAINER_SELECTOR, 4)), 1)
            self.assertEqual(client.reads.count((0x80410070, 0xB18)), 1)
            self.assertEqual(client.reads.count((PROBE.REPLACEMENT_NAME_POINTER, 4)), 1)
            self.assertEqual(client.reads.count((0x80420000, 22)), 1)
            self.assertEqual(client.reads.count((client.hero_address, 0xB18)), 2)
            self.assertLessEqual(PROBE.HERO_SIZE, PROBE.MAX_READ)
            invalid = HeroMemory(dol, entry)
            invalid.hero_address += 1
            with self.assertRaises(PROBE.ProbeError):
                PROBE.observe(invalid, dol, probes, stops)
            self.assertFalse(any(size == PROBE.HERO_SIZE for _, size in invalid.reads))
            wrong_frame = HeroMemory(dol, entry)
            wrong_frame.wrong_return_stack = True
            with self.assertRaisesRegex(PROBE.ProbeError, 'return PC/stack'):
                PROBE.observe(wrong_frame, dol, probes, stops)
            self.assertEqual(wrong_frame.reads.count((wrong_frame.hero_address, 0xB18)), 1)
            self.assertEqual(wrong_frame.reads.count((0x816FFEB8, 312)), 1)

    def test_post_hero_lifecycle_records_attest_at_entry_and_return(self):
        with tempfile.TemporaryDirectory() as directory:
            dol = synthetic_dol(directory, 'postHeroPreRoster')
            entry = PROBE.CANDIDATES['postHeroPreRoster']
            plan = {'schema': 1, 'probes': [{'name': 'postHeroPreRoster', 'capture_return': True,
                    'caller_addresses': [hex(entry - 0x10)]}]}
            probes, stops, _ = PROBE.validate_plan(plan, dol)
            class AttestLifecycleMemory(MemoryClient):
                def read_memory(self, address, size):
                    if address in (PROBE.ATTEST_POINTER, 0x80410000):
                        self.reads.append((address, size))
                        self.assert_size(size)
                        return bytes.fromhex('80410000' if address == PROBE.ATTEST_POINTER else '01020304')
                    return super().read_memory(address, size)

                @staticmethod
                def assert_size(size):
                    if size != 4:
                        raise AssertionError('attest read must be four bytes')
            client = AttestLifecycleMemory(dol, entry)
            result = PROBE.observe(client, dol, probes, stops)
            row = result['records'][0]
            self.assertEqual(row['entry']['attest']['data_hex'], '01020304')
            self.assertEqual(row['return']['attest']['data_hex'], '01020304')
            self.assertEqual(client.reads.count((PROBE.ATTEST_POINTER, 4)), 2)
            self.assertEqual(client.reads.count((0x80410000, 4)), 2)

    def test_attest_reads_independent_savedata_pointer_and_four_bytes(self):
        class AttestMemory:
            def __init__(self, attest_pointer=0x80410000):
                self.attest_pointer = attest_pointer
                self.reads = []

            def read_memory(self, address, size):
                self.reads.append((address, size))
                if address == PROBE.RNG_POINTER and size == 4:
                    return bytes.fromhex('80400000')
                if address == 0x80400000 and size == 4:
                    return bytes.fromhex('12345678')
                if address == 0x80400200 and size == 312:
                    return bytes.fromhex('deadbeef') * 78
                if address == PROBE.ATTEST_POINTER and size == 4:
                    return self.attest_pointer.to_bytes(4, 'big')
                if address == 0x80410000 and size == 4:
                    return bytes.fromhex('01234567')
                raise AssertionError(f'unexpected memory read {address:08x},{size}')

        client = AttestMemory()
        objects = [{'name': 'pokemon', 'register': 'r4', 'address': 0x80400200, 'size': 312}]
        result = PROBE.snapshot(client, {'r4': 0x80400200}, objects, 'postHeroPreRoster')
        self.assertEqual(client.reads, [(PROBE.RNG_POINTER, 4), (0x80400000, 4),
                                      (0x80400200, 312), (0x8047ADB8, 4), (0x80410000, 4)])
        self.assertEqual(result['attest'], {'pointer_address': '0x8047adb8',
                         'pointee_address': '0x80410000', 'offset': 0, 'data_hex': '01234567'})
        self.assertNotEqual(result['attest']['data_hex'], result['objects'][0]['data_hex'][:8])
        # Snapshot again: the pointer and pointee must be read again, not reused.
        PROBE.snapshot(client, {'r4': 0x80400200}, objects, 'postHeroPreRoster')
        self.assertEqual(client.reads.count((PROBE.ATTEST_POINTER, 4)), 2)
        self.assertEqual(client.reads.count((0x80410000, 4)), 2)
        for invalid in (0, 0x7FFFFFFF, 0x817FFFFD, 0x81800000, 0xFFFFFFFF):
            with self.subTest(pointer=invalid):
                bad = AttestMemory(invalid)
                with self.assertRaises(PROBE.ProbeError):
                    PROBE.snapshot(bad, {}, [], 'postHeroPreRoster')
                self.assertEqual(bad.reads[-1], (PROBE.ATTEST_POINTER, 4))
        # Other candidates and legacy snapshots do not invent this evidence.
        legacy = AttestMemory()
        self.assertNotIn('attest', PROBE.snapshot(legacy, {}, []))
        self.assertNotIn((PROBE.ATTEST_POINTER, 4), legacy.reads)

    def test_loopback_and_guest_ranges(self):
        for value in ('127.0.0.1:55001', '[::1]:55001'):
            self.assertEqual(PROBE.tcp_endpoint(value)[1], 55001)
        for value in ('localhost:55001', '0.0.0.0:55001', '192.168.1.2:55001',
                      'example.com:1234', '127.0.0.1:0', '127.0.0.1:65536'):
            with self.subTest(value=value), self.assertRaises(PROBE.ProbeError):
                PROBE.tcp_endpoint(value)
        self.assertEqual(PROBE.mem1(0x817FFFFC, 4), 0x817FFFFC)
        for address, size in ((0x817FFFFF, 2), (0x7FFFFFFF, 1), (0x80000000, 0),
                              (0x80000000, 4097), (0xFFFFFFFF, 4)):
            with self.assertRaises(PROBE.ProbeError):
                PROBE.mem1(address, size)

    def test_dol_pin_and_direct_call_context(self):
        with tempfile.TemporaryDirectory() as directory:
            dol = synthetic_dol(directory)
            entry = PROBE.CANDIDATES['opponentTrainerCreate']
            caller = entry - 0x10
            self.assertEqual(PROBE.linked_target(caller, dol.word(caller)), entry)
            with self.assertRaises(PROBE.ProbeError):
                PROBE.OriginalDOL(Path(directory) / 'synthetic.dol', '0' * 64)
            with self.assertRaises(PROBE.ProbeError):
                dol.word(0x80000000)
            with self.assertRaises(PROBE.ProbeError):
                PROBE.linked_target(entry, dol.word(entry))

    def test_entry_return_captures_same_object_and_pointed_rng(self):
        with tempfile.TemporaryDirectory() as directory:
            dol = synthetic_dol(directory)
            entry = PROBE.CANDIDATES['opponentTrainerCreate']
            plan = {'schema': 1, 'probes': [{'name': 'opponentTrainerCreate', 'capture_return': True,
                    'caller_addresses': [hex(entry - 0x10)],
                    'objects': [{'name': 'pokemon', 'register': 'r4', 'size': 312}]}]}
            probes, stops, _ = PROBE.validate_plan(plan, dol)
            client = MemoryClient(dol, entry)
            result = PROBE.observe(client, dol, probes, stops)
            row = result['records'][0]
            self.assertEqual(row['entry']['rng']['seed'], '0x12345678')
            self.assertEqual(row['return']['objects'][0]['address'], '0x80400100')
            self.assertFalse(result['full_battle_qualified'])
            self.assertEqual(client.breakpoints, set())
            self.assertNotIn((0x80478C90, 4), client.reads)
            self.assertEqual([cmd for cmd in client.commands if cmd.startswith(('Z', 'z'))],
                             [f'Z0,{entry:08x},4', f'z0,{entry:08x},4',
                              f'Z0,{entry - 0xC:08x},4', f'z0,{entry - 0xC:08x},4'])
            # No invalid RNG pointer silently becomes the adjacent static fallback.
            bad = MemoryClient(dol, entry, pointer=0)
            with self.assertRaises(PROBE.ProbeError):
                PROBE.observe(bad, dol, probes, stops)

    def test_consecutive_same_candidate_gets_distinct_capture_ids(self):
        with tempfile.TemporaryDirectory() as directory:
            dol = synthetic_dol(directory)
            plan = {'schema': 1, 'max_stops': 4, 'max_seconds': 600,
                    'probes': [{'name': 'opponentTrainerCreate', 'capture_return': True,
                                'caller_addresses': [hex(PROBE.CANDIDATES['opponentTrainerCreate'] - 0x10)]}] * 2}
            probes, stops, seconds = PROBE.validate_plan(plan, dol)
            self.assertEqual(seconds, 600)
            result = PROBE.observe(MemoryClient(dol, probes[0]['entry']), dol, probes, stops)
            self.assertEqual(result['stops'], 4)
            self.assertEqual([row['capture_id'] for row in result['records']],
                             ['00-opponentTrainerCreate', '01-opponentTrainerCreate'])

    def test_guest_pin_wrong_pc_and_unadmitted_return_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            dol = synthetic_dol(directory)
            entry = PROBE.CANDIDATES['opponentTrainerCreate']
            plan = {'schema': 1, 'probes': [{'name': 'opponentTrainerCreate', 'capture_return': True,
                    'caller_addresses': [hex(entry - 0x10)]}]}
            probes, stops, _ = PROBE.validate_plan(plan, dol)
            class WrongRegisters(MemoryClient):
                def registers(self):
                    registers = super().registers()
                    registers[register] += 4
                    return registers
            for register in ('pc', 'lr'):
                with self.subTest(register=register), self.assertRaises(PROBE.ProbeError):
                    PROBE.observe(WrongRegisters(dol, entry), dol, probes, stops)
            class WrongPin(MemoryClient):
                def read_memory(self, address, size):
                    return b'\x00' * size
            with self.assertRaisesRegex(PROBE.ProbeError, 'instruction differs'):
                PROBE.observe(WrongPin(dol, entry), dol, probes, stops)

    def test_plan_and_context_errors_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            dol = synthetic_dol(directory)
            base = {'schema': 1, 'probes': [{'name': 'opponentTrainerCreate'}]}
            for mutation in ('return', 'caller', 'name', 'register', 'size', 'stops', 'seconds', 'extra'):
                plan = copy.deepcopy(base)
                probe = plan['probes'][0]
                if mutation == 'return': probe['capture_return'] = True
                elif mutation == 'caller': probe['caller_addresses'] = [hex(PROBE.CANDIDATES['opponentTrainerCreate'])]
                elif mutation == 'name': probe['name'] = 'arbitraryAddress'
                elif mutation in ('register', 'size'):
                    probe['objects'] = [{'name': 'pokemon', 'register': 'r99' if mutation == 'register' else 'r4',
                                         'size': 313 if mutation == 'size' else 312}]
                elif mutation == 'stops': plan['max_stops'] = 0
                elif mutation == 'seconds': plan['max_seconds'] = 3601
                else: probe['callback'] = 'seed + 1'
                with self.subTest(mutation=mutation), self.assertRaises(PROBE.ProbeError):
                    PROBE.validate_plan(plan, dol)
            with self.assertRaises(PROBE.ProbeError):
                PROBE.validate_source(Path(directory) / 'synthetic.dol')

    def test_private_output_refuses_overwrite_and_checkout(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertEqual(PROBE.private_output(root / 'new', root), root / 'new')
            (root / 'exists').mkdir()
            with self.assertRaises(PROBE.ProbeError):
                PROBE.private_output(root / 'exists', root)
            repository = Path(PROBE.__file__).resolve().parents[2]
            with self.assertRaises(PROBE.ProbeError):
                PROBE.private_output(repository / 'capture', repository)

    def test_atomic_directory_publication_never_replaces_existing_empty_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            stage, output = root / 'stage', root / 'output'
            stage.mkdir()
            (stage / 'complete.json').write_text('{}')
            output.mkdir()
            with self.assertRaises(OSError):
                PROBE.publish_new_directory(stage, output)
            self.assertTrue(stage.is_dir())
            self.assertEqual(list(output.iterdir()), [])
            output.rmdir()
            PROBE.publish_new_directory(stage, output)
            self.assertFalse(stage.exists())
            self.assertTrue((output / 'complete.json').is_file())


if __name__ == '__main__':
    unittest.main()
