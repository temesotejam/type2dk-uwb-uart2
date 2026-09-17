import binascii
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location('check_log', Path(__file__).resolve().parents[1]/'scripts/check_log.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def record(seq):
    body = f'UWB_EVENT,port=A,seq=42,log_seq={seq}'.encode()
    return body + f',log_crc={binascii.crc_hqx(body, 0xffff):04x}\r\n'.encode()


first, second, third = record(1), record(2), record(3)
assert module.check(first+second+third)['valid'] == 3
assert module.check(first+third)['missing_records'] == 1
assert module.check(first+first)['duplicate_records'] == 1
assert module.check(record(0xffffffff)+record(0))['missing_records'] == 0
assert module.check(third+first)['resets_or_backwards'] == 1
# One-byte deletions inside a record, and a lost newline joining records.
for i in range(len(second)-2):
    report = module.check(first+second[:i]+second[i+1:]+third)
    assert report['invalid_lines'] == [2]
    assert report['missing_records'] == 1
assert module.check(first[:-2]+second)['invalid_lines'] == [1]
print('USB raw log corruption tests passed')
