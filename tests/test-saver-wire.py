#!/usr/bin/env python3
"""Check boundaries and compare the estimator with vendored packetizer math."""
from pathlib import Path
import os
import random
import subprocess
import tarfile
import tempfile
import unittest
import sys

sys.dont_write_bytecode = True

from saver_wire import estimate_frame, estimate_run

ROOT = Path(__file__).resolve().parents[1]
checkout = subprocess.check_output(['bash', str(ROOT / 'scripts/build-paths.sh'), 'checkout'], text=True).strip()
OUTPUT = Path(os.environ.get('DESKPORT_TEST_DIR', Path(checkout) / 'tests'))
OUTPUT.mkdir(parents=True, exist_ok=True)


def function(source, marker):
    start = source.index(marker)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class WireTest(unittest.TestCase):
    def test_padding_and_small_frame_minimum(self):
        tiny = estimate_frame(1, packet_size=896)
        self.assertEqual((tiny.data_shards, tiny.parity_shards, tiny.packets), (1, 1, 2))
        self.assertEqual(tiny.video_ip_bytes, 2 * 940)
        self.assertEqual(estimate_frame(872, packet_size=896).data_shards, 1)  # +8 fills 880.
        self.assertEqual(estimate_frame(873, packet_size=896).data_shards, 2)
        protected = estimate_frame(1, packet_size=896, min_parity=2)
        self.assertEqual(protected.video_ip_bytes, 3 * 940)
        self.assertEqual(estimate_frame(1, packet_size=896, fec_percentage=0, min_parity=2).packets, 1)

    def test_block_boundaries_and_huge_frame_fec_disable(self):
        one = estimate_frame(212 * 880 - 8, packet_size=896)
        self.assertEqual(one.fec_blocks, ((212, 43),))
        two = estimate_frame(212 * 880 - 7, packet_size=896)
        self.assertEqual(two.fec_blocks, ((107, 22), (106, 22)))
        four = estimate_frame(848 * 880 - 8, packet_size=896)
        self.assertEqual(len(four.fec_blocks), 4)
        huge = estimate_frame(848 * 880 - 7, packet_size=896)
        self.assertTrue(huge.fec_disabled_for_oversize)
        self.assertEqual(huge.parity_shards, 0)
        self.assertEqual(len(huge.fec_blocks), 4)
        broken = estimate_frame(4096 * 880, packet_size=896)
        self.assertFalse(broken.protocol_recoverable)

    def test_encryption_ip_and_budget_scope(self):
        plain = estimate_frame(450, packet_size=896)
        encrypted = estimate_frame(450, packet_size=896, encrypted=True, ip_version=6)
        self.assertEqual(encrypted.video_ip_bytes - plain.video_ip_bytes, 2 * (32 + 20))
        run = estimate_run([450] * 60, duration_seconds=60, packet_size=896)
        self.assertEqual(run['video_ip_bytes'], 60 * 1880)
        self.assertEqual(run['estimated_video_ip_mb_per_hour'], 6.768)
        self.assertTrue(run['video_ip_budget_met'])
        self.assertFalse(run['carrier_total_verified'])
        self.assertGreater(estimate_run([100000] * 600, duration_seconds=60, packet_size=896)['estimated_video_ip_mb_per_hour'], 300)
        for params in ({'encoded_bytes': -1}, {'encoded_bytes': 1.5}, {'encoded_bytes': 0, 'packet_size': 16}):
            with self.assertRaises(ValueError):
                estimate_frame(**params)

    def test_actual_vendored_packetization_math(self):
        # Use the actual concat/split/FEC arithmetic from each pinned source.
        # No network, Reed-Solomon buffers, media data or installed host is used.
        rng = random.Random(7362)
        cases = [(n, p, f, m) for p in (896, 1392) for f in (0, 20, 50)
                 for m in (1, 2) for n in (0, 1, p - 24, p - 23, 100000, 300000, 900000)]
        cases += [(rng.randrange(1000000), 896, 20, 1) for _ in range(30)]
        for archive in ('sunshine.tar.gz', 'sunshine-nix.tar.gz'):
            with tarfile.open(ROOT / 'host/vendor' / archive) as stream:
                member = next(m for m in stream.getmembers() if m.name == 'src/stream.cpp' or m.name.endswith('/src/stream.cpp'))
                source = stream.extractfile(member).read().decode()
            concat = function(source, '  std::vector<uint8_t> concat_and_insert(')
            fec_start = source.index('      auto payload_size = payload.size();', source.index('    static fec_t encode('))
            fec_end = source.index('      // If we need to store a zero-padded data shard', fec_start)
            fec_math = source[fec_start:fec_end]
            split_start = source.index('      // There are 2 bits for FEC block count')
            split_end = source.index('\n      try {', split_start)
            split = source[split_start:split_end]
            program = '''#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
using namespace std::string_view_literals;
struct Log { template<typename T> Log& operator<<(const T&) { return *this; }
Log& operator<<(std::ostream&(*)(std::ostream&)) { return *this; } };
#define BOOST_LOG(level) Log{}
constexpr int DATA_SHARDS_MAX = 255;
''' + concat + '''
std::array<size_t,2> fec(std::string_view payload, size_t blocksize, size_t fecpercentage, size_t minparityshards) {
''' + fec_math + '''
return {data_shards, parity_shards}; }
int main() { size_t n, packetSize, fecPercentage, minParity;
while (std::cin >> n >> packetSize >> fecPercentage >> minParity) {
  const size_t blocksize = packetSize + 16;
  const std::string header(8, 'x'), elementary(n, 'x');
  auto framed = concat_and_insert(32, blocksize - 32, header, elementary);
  std::string_view payload{reinterpret_cast<const char*>(framed.data()), framed.size()};
''' + split + '''
  size_t data=0, parity=0;
  for (auto it=fec_blocks_begin; it!=fec_blocks_end; ++it) {
    auto counts=fec(*it,blocksize,fecPercentage,minParity); data+=counts[0]; parity+=counts[1];
  }
  std::cout << data << " " << parity << "\\n";
}}
'''
            with tempfile.TemporaryDirectory(prefix='saver-wire-', dir=OUTPUT) as temporary:
                work = Path(temporary); cpp = work / 'oracle.cpp'; binary = work / 'oracle'
                cpp.write_text(program)
                subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', str(cpp), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], input=''.join(f'{n} {p} {f} {m}\n' for n, p, f, m in cases),
                                        text=True, capture_output=True, check=True)
            actual = [tuple(map(int, line.split())) for line in result.stdout.splitlines()]
            expected = [(estimate_frame(n, packet_size=p, fec_percentage=f, min_parity=m).data_shards,
                         estimate_frame(n, packet_size=p, fec_percentage=f, min_parity=m).parity_shards)
                        for n, p, f, m in cases]
            self.assertEqual(actual, expected, archive)


if __name__ == '__main__':
    unittest.main()
