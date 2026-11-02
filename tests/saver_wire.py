#!/usr/bin/env python3
"""Estimate Sunshine video IP bytes, not elementary-stream or carrier bytes.

packet_size means the SDP/Sunshine session.config.packetsize, after any client
encryption adjustment. Sunshine adds 16 bytes, inserts a 32-byte RTP/NV header
per data shard, zero-pads every shard, and sends FEC parity at the same size.
The formulas follow fec::encode() and videoBroadcastThread() in both vendored
src/stream.cpp revisions. Supply post-rewrite elementary sizes for exactness;
raw AVPacket sizes omit any later SPS/VPS replacement-size differences.
"""
import argparse
import csv
from dataclasses import dataclass
import json
import math
from pathlib import Path

SHORT_FRAME_HEADER = 8
RTP_NV_HEADER = 32
MAX_RTP_HEADER = 16
ENCRYPTION_PREFIX = 32
MAX_FEC_SHARDS = 255
MAX_FEC_BLOCKS = 4


def ceil_div(value, divisor):
    return (value + divisor - 1) // divisor


@dataclass(frozen=True)
class FrameEstimate:
    elementary_bytes: int
    data_shards: int
    parity_shards: int
    packets: int
    fec_blocks: tuple
    fec_percentage: int
    fec_disabled_for_oversize: bool
    protocol_recoverable: bool
    udp_payload_per_packet: int
    udp_payload_bytes: int
    video_ip_bytes: int


def estimate_frame(encoded_bytes, *, packet_size=1392, fec_percentage=20,
                   min_parity=1, ip_version=4, encrypted=False,
                   ip_header_bytes=None, extra_encoded_bytes=0):
    """Count one frame; encoded_bytes excludes the eight-byte short header.

    min_parity=1 models saver on a clean link, 2 models the normal viewer
    minimum/restored loss protection. The caller must choose the actual state.
    FEC disabled for huge frames also disables the minimum, as in Sunshine.
    """
    for name, value in [('encoded_bytes', encoded_bytes), ('packet_size', packet_size),
                        ('fec_percentage', fec_percentage), ('min_parity', min_parity),
                        ('extra_encoded_bytes', extra_encoded_bytes)]:
        if type(value) is not int or value < 0:
            raise ValueError(f'{name} must be a nonnegative integer')
    if packet_size <= RTP_NV_HEADER - MAX_RTP_HEADER or packet_size > 65000:
        raise ValueError('packet_size is outside the usable UDP payload range')
    if fec_percentage > 255 or min_parity > 255 or ip_version not in (4, 6):
        raise ValueError('invalid FEC or IP parameters')
    ip_header = (20 if ip_version == 4 else 40) if ip_header_bytes is None else ip_header_bytes
    if type(ip_header) is not int or ip_header < (20 if ip_version == 4 else 40):
        raise ValueError('invalid IP header length')
    blocksize = packet_size + MAX_RTP_HEADER
    body_per_shard = blocksize - RTP_NV_HEADER
    elementary = encoded_bytes + extra_encoded_bytes
    frame_payload = elementary + SHORT_FRAME_HEADER
    # concat_and_insert() adds a packet header before every slice, leaving the
    # final slice unpadded until fec::encode() allocates its full-sized shard.
    slices = ceil_div(frame_payload, body_per_shard)
    payload_size = frame_payload + slices * RTP_NV_HEADER
    max_data = MAX_FEC_SHARDS * 100 // (100 + fec_percentage)
    blocks_needed = ceil_div(payload_size, max_data * blocksize)
    oversize = blocks_needed > MAX_FEC_BLOCKS
    actual_fec = 0 if oversize else fec_percentage
    blocks_needed = min(blocks_needed, MAX_FEC_BLOCKS)
    aligned_size = ceil_div(payload_size // blocks_needed, blocksize) * blocksize
    blocks = []
    for index in range(blocks_needed):
        size = payload_size - index * aligned_size if index == blocks_needed - 1 else aligned_size
        data = ceil_div(size, blocksize)
        parity = ceil_div(data * actual_fec, 100)
        if actual_fec and parity < min_parity:
            parity = min_parity
        blocks.append((data, parity))
    data = sum(item[0] for item in blocks)
    parity = sum(item[1] for item in blocks)
    packets = data + parity
    udp_size = blocksize + (ENCRYPTION_PREFIX if encrypted else 0)
    recoverable = all(d < 1024 and (actual_fec == 0 or d + p <= MAX_FEC_SHARDS) for d, p in blocks)
    return FrameEstimate(elementary, data, parity, packets, tuple(blocks), actual_fec,
                         oversize, recoverable, udp_size, packets * udp_size,
                         packets * (udp_size + 8 + ip_header))


def estimate_run(encoded_sizes, *, duration_seconds, budget_mb_per_hour=300,
                 vpn_extra_bytes_per_packet=0, **parameters):
    if not math.isfinite(duration_seconds) or duration_seconds <= 0 or vpn_extra_bytes_per_packet < 0:
        raise ValueError('duration must be positive and tunnel overhead nonnegative')
    estimates = [estimate_frame(size, **parameters) for size in encoded_sizes]
    packets = sum(item.packets for item in estimates)
    ip_bytes = sum(item.video_ip_bytes for item in estimates)
    mb_per_hour = ip_bytes * 3600 / duration_seconds / 1_000_000
    return {
        'frames': len(estimates), 'packets': packets, 'duration_seconds': duration_seconds,
        'elementary_bytes': sum(item.elementary_bytes for item in estimates),
        'video_udp_payload_bytes': sum(item.udp_payload_bytes for item in estimates),
        'video_ip_bytes': ip_bytes, 'estimated_video_ip_mb_per_hour': mb_per_hour,
        'budget_mb_per_hour': budget_mb_per_hour,
        'video_ip_budget_met': mb_per_hour <= budget_mb_per_hour,
        'protocol_recoverable': all(item.protocol_recoverable for item in estimates),
        'assumed_vpn_extra_bytes_per_packet': vpn_extra_bytes_per_packet,
        'video_plus_assumed_vpn_mb_per_hour': (ip_bytes + packets * vpn_extra_bytes_per_packet) * 3600 / duration_seconds / 1_000_000,
        'carrier_total_verified': False,
        'limitations': [
            'Video IP datagrams only; audio, control, input, link-layer overhead and retransmission are excluded.',
            'VPN overhead is excluded unless an explicit per-packet assumption is supplied; WireGuard padding/outer IP and relay TCP/TLS/retransmissions vary.',
            'Raw AVPacket sizes are exact only when later SPS/VPS replacements do not change size; post-rewrite sizes or an explicit extra-byte allowance are needed otherwise.',
            'Projection extrapolates this finite workload, not every future hour or carrier billing.',
        ],
    }


def calibrate_capture(frames, *, fec_percentage=20, min_parity=1, ip_version=4, encrypted=False):
    """Compare frame-total header metadata without reading pixel payloads.

    A multi-block capture parser must aggregate its blocks first; data_shards
    and encoded_bytes here describe the complete frame, not just block zero.
    """
    if not frames:
        raise ValueError('capture has no frames')
    mismatches = []
    predicted_ip_bytes = predicted_packets = 0
    for index, frame in enumerate(frames):
        # The header parser's encoded_bytes already includes the short header.
        estimate = estimate_frame(int(frame['encoded_bytes']) - SHORT_FRAME_HEADER,
                                  packet_size=int(frame['udp_payload']) - MAX_RTP_HEADER - (ENCRYPTION_PREFIX if encrypted else 0),
                                  fec_percentage=fec_percentage, min_parity=min_parity,
                                  ip_version=ip_version, encrypted=encrypted)
        predicted_ip_bytes += estimate.video_ip_bytes
        predicted_packets += estimate.packets
        actual = (frame['data_shards'], frame['packets'], frame['wire_ip_bytes'])
        predicted = (estimate.data_shards, estimate.packets, estimate.video_ip_bytes)
        if actual != predicted:
            mismatches.append({'frame_ordinal': index, 'actual': actual, 'estimated': predicted})
    duration = max(frame['last'] for frame in frames) - min(frame['first'] for frame in frames)
    ip_bytes = sum(frame['wire_ip_bytes'] for frame in frames)
    if duration <= 0:
        raise ValueError('capture interval must be positive')
    return {'frames': len(frames), 'actual_packets': sum(frame['packets'] for frame in frames),
            'estimated_packets': predicted_packets, 'estimated_video_ip_bytes': predicted_ip_bytes,
            'actual_video_ip_bytes': ip_bytes, 'duration_seconds': duration,
            'actual_video_ip_mb_per_hour': ip_bytes * 3600 / duration / 1_000_000,
            'estimated_video_ip_mb_per_hour': predicted_ip_bytes * 3600 / duration / 1_000_000,
            'exact_frames': len(frames) - len(mismatches), 'mismatches': mismatches,
            'encoded_short_header_subtracted_bytes': SHORT_FRAME_HEADER,
            'fec_percentage': fec_percentage, 'min_parity': min_parity,
            'note': 'First-to-last captured-packet interval; metadata calibration at IP layer, not VPN/carrier bytes.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--packets-csv', type=Path)
    source.add_argument('--capture-json', type=Path)
    parser.add_argument('--bytes-column', default='bytes')
    parser.add_argument('--encoded-header-bytes', type=int, choices=(0, 8), default=0)
    parser.add_argument('--duration-seconds', type=float)
    parser.add_argument('--packet-size', type=int, default=1392)
    parser.add_argument('--fec-percentage', type=int, default=20)
    parser.add_argument('--min-parity', type=int, default=1)
    parser.add_argument('--ip-version', type=int, choices=(4, 6), default=4)
    parser.add_argument('--encrypted', action='store_true')
    parser.add_argument('--vpn-extra-bytes-per-packet', type=int, default=0)
    parser.add_argument('--budget-mb-per-hour', type=float, default=300)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.capture_json:
        result = calibrate_capture(json.loads(args.capture_json.read_text()),
                                   fec_percentage=args.fec_percentage, min_parity=args.min_parity,
                                   ip_version=args.ip_version, encrypted=args.encrypted)
    else:
        if not args.duration_seconds:
            parser.error('--duration-seconds is required for CSV workload projection')
        with args.packets_csv.open(newline='') as stream:
            rows = list(csv.DictReader(stream))
        result = estimate_run([int(row[args.bytes_column]) - args.encoded_header_bytes for row in rows],
                              duration_seconds=args.duration_seconds, packet_size=args.packet_size,
                              fec_percentage=args.fec_percentage, min_parity=args.min_parity,
                              ip_version=args.ip_version, encrypted=args.encrypted,
                              vpn_extra_bytes_per_packet=args.vpn_extra_bytes_per_packet,
                              budget_mb_per_hour=args.budget_mb_per_hour)
        result['packet_size_semantics'] = 'Sunshine/SDP packetSize, not UDP payload size'
        result['packet_size'] = args.packet_size
        result['min_parity'] = args.min_parity
    text = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end='')


if __name__ == '__main__':
    main()
