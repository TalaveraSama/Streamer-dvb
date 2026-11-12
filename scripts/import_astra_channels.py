#!/usr/bin/env python3
"""Prepare (never install) a channels-only Cesbo Astra migration.

No Astra strings are executed. Output is a private, separate copy of the target
config; the active service and source files are never modified.
"""
import argparse
import copy
import json
import os
import re
import sys
from collections import Counter
from urllib.parse import parse_qs, urlencode, urlsplit

MAX_BYTES = 64 * 1024 * 1024
SAFE_ID = re.compile(r'^[A-Za-z0-9_-]{1,64}$')


def load_json(path):
    with open(path, 'rb') as f:
        raw = f.read(MAX_BYTES + 1)
    if len(raw) > MAX_BYTES:
        raise ValueError('JSON exceeds 64 MiB')
    value = json.loads(raw)
    if not isinstance(value, dict):
        raise ValueError('JSON root must be an object')
    return value


def integer(value, low, high):
    if isinstance(value, bool) or not re.fullmatch(r'\d+', str(value)):
        raise ValueError('invalid numeric value')
    result = int(value)
    if not low <= result <= high:
        raise ValueError('numeric value out of range')
    return result


def tuner_uri(tuner, options, diseqc):
    adapter = integer(tuner.get('adapter'), 0, 31)
    frontend = integer(tuner.get('device', 0), 0, 31)
    # Astra exports frequency in MHz; the panel expects integer kHz.
    frequency = integer(tuner.get('frequency'), 900, 14000) * 1000
    rate = integer(tuner.get('symbolrate'), 100, 60000)
    polarity = str(tuner.get('polarization', '')).upper()
    delivery = {'S':'dvb-s', 'S2':'dvb-s2'}.get(str(tuner.get('type', '')).upper())
    if polarity not in ('H', 'V') or not delivery:
        raise ValueError('invalid DVB tuning parameters')
    modulation = str(tuner.get('modulation', 'auto')).lower()
    modulation = {'psk8':'8psk', 'psk16':'16apsk', 'psk32':'32apsk'}.get(modulation, modulation)
    if modulation not in ('auto', 'qpsk', '8psk', '16apsk', '32apsk'):
        raise ValueError('unsupported DVB modulation')
    fec = str(tuner.get('fec', 'auto')).lower()
    if fec not in ('auto','1/2','2/3','3/4','4/5','5/6','6/7','7/8','8/9','9/10','3/5','2/5'):
        raise ValueError('unsupported DVB FEC')
    params = {
        'adapter':adapter, 'frontend':frontend, 'frequency_khz':frequency,
        'symbol_rate':rate, 'polarity':polarity, 'delivery_system':delivery,
        'modulation':modulation, 'fec':fec, 'diseqc_source':diseqc,
        'lnb_lof1_khz':options.lof1 * 1000, 'lnb_lof2_khz':options.lof2 * 1000,
        'lnb_slof_khz':options.slof * 1000, 'stream_id':-1, 'pids':8192,
    }
    return 'dvb://satellite?' + urlencode(params)


def identity(uri, sid):
    parts = urlsplit(uri)
    if parts.scheme.lower() == 'dvb':
        q = parse_qs(parts.query)
        keys = ('adapter', 'frontend', 'frequency_khz', 'symbol_rate', 'polarity', 'delivery_system')
        return ('dvb', *(q.get(key, [''])[0].lower() for key in keys), str(sid))
    return ('url', uri)


def safe_http(uri):
    try:
        parsed = urlsplit(uri)
        # Do not copy embedded credentials, tokens, fragments or executable URLs.
        if (parsed.scheme.lower() not in ('http', 'https') or not parsed.hostname
                or parsed.username is not None or parsed.password is not None
                or parsed.query or parsed.fragment or any(ord(c) < 33 or c in '<>\"\'`' for c in uri)):
            return False
        if parsed.port is not None and not 1 <= parsed.port <= 65535:
            return False
        return True
    except ValueError:
        return False


def make_plan(source, target, options):
    channels = source.get('make_stream')
    tunes = source.get('dvb_tune', [])
    existing = target.get('streams')
    if not isinstance(channels, list) or not isinstance(tunes, list) or not isinstance(existing, list):
        raise ValueError('Expected Astra make_stream/dvb_tune and target streams arrays')
    tuners = {}
    duplicate_tuners = set()
    for item in tunes:
        if not isinstance(item, dict) or not isinstance(item.get('id'), str):
            continue
        tid = item['id']
        if tid in tuners:
            duplicate_tuners.add(tid)
        tuners[tid] = item
    for tid in duplicate_tuners:
        del tuners[tid]

    used_ids = {x.get('id') for x in existing if isinstance(x, dict)}
    used_ports = set()
    used_inputs = set()
    for x in existing:
        if not isinstance(x, dict):
            continue
        uri = x.get('input_uri', '')
        if isinstance(uri, str) and uri:
            used_inputs.add(identity(uri, x.get('input_service_id', x.get('service_id', 1))))
        outputs = [x]
        for field in ('additional_outputs', 'outputs'):
            if isinstance(x.get(field), list):
                outputs += x[field]
        for out in outputs:
            if isinstance(out, dict) and out.get('output_host') == '127.0.0.1':
                try:
                    used_ports.add(integer(out.get('output_port'), 1, 65535))
                except ValueError:
                    pass

    report = Counter()
    planned = []
    port = options.base_port
    for item in channels:
        if not isinstance(item, dict):
            report['invalid_record'] += 1
            continue
        inputs = item.get('input')
        if not isinstance(inputs, list) or len(inputs) != 1 or not isinstance(inputs[0], str):
            report['unsupported_inputs'] += 1
            continue
        source_uri = inputs[0]
        parsed = urlsplit(source_uri)
        scheme = parsed.scheme.lower()
        sid = 0
        if scheme == 'dvb':
            if not options.confirm_adapters or None in (options.lof1, options.lof2, options.slof):
                report['dvb_needs_adapter_and_lnb_confirmation'] += 1
                continue
            # Astra uses the fragment for PNR and CAM. Only the PNR is consumed.
            tuner_id = parsed.netloc
            tuner = tuners.get(tuner_id)
            query = parse_qs(parsed.fragment, keep_blank_values=True)
            if not tuner or parsed.path or parsed.query or len(query.get('pnr', [])) != 1:
                report['invalid_or_missing_tuner'] += 1
                continue
            try:
                sid = integer(query['pnr'][0], 1, 65535)
                # DiSEqC numbering differs between applications. Never guess it.
                if 'diseqc' in tuner and tuner_id not in options.diseqc_map:
                    report['dvb_needs_diseqc_mapping'] += 1
                    continue
                uri = tuner_uri(tuner, options, options.diseqc_map.get(tuner_id, -1))
            except (ValueError, TypeError):
                report['invalid_tuning'] += 1
                continue
            kind = 'dvb'
        elif scheme in ('http', 'https') and safe_http(source_uri):
            uri = source_uri
            kind = 'http'
        else:
            report['rejected_unsafe_or_unsupported'] += 1
            continue
        original_id = item.get('id', '')
        name = item.get('name', '')
        if (not isinstance(original_id, str) or not SAFE_ID.fullmatch(original_id)
                or not isinstance(name, str) or not name.strip() or len(name) > 200
                or any(ord(c) < 32 or c in '<>\"\'`' for c in name)):
            report['invalid_id_or_name'] += 1
            continue
        stream_id = 'astra-' + original_id
        key = identity(uri, sid)
        if stream_id in used_ids or key in used_inputs:
            report['duplicate'] += 1
            continue
        while port in used_ports and port <= 65535:
            port += 1
        if port > 65535:
            report['no_free_placeholder_port'] += 1
            continue
        # Loopback output is a placeholder, not an Astra output. Everything is
        # stopped by default, including streams that were enabled in Astra.
        entry = {'id':stream_id, 'name':name, 'input_uri':uri, 'input_mode':'auto',
                 'input_service_id':sid, 'service_id':sid if sid else 1,
                 'service_name':name, 'output_type':'udp-vbr', 'output_mode':'listener',
                 'output_host':'127.0.0.1', 'output_port':port,
                 'auto_start':False, 'remap_enabled':False, 'transcode_enabled':False,
                 'conditional_access_client':''}
        planned.append(entry)
        used_ids.add(stream_id)
        used_inputs.add(key)
        used_ports.add(port)
        port += 1
        report['importable'] += 1
        report[kind + '_importable'] += 1
        if item.get('enable') is False:
            report['source_disabled'] += 1
        if item.get('output'):
            report['source_outputs_not_imported'] += 1
    result = copy.deepcopy(target)
    result['streams'].extend(planned)
    return result, report


def write_new(path, data):
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, 'O_NOFOLLOW'):
        flags |= os.O_NOFOLLOW
    fd = os.open(path, flags, 0o600)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as out:
            json.dump(data, out, ensure_ascii=False, indent=2)
            out.write('\n')
    except BaseException:
        os.unlink(path)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('astra', help='Astra export JSON (read only)')
    parser.add_argument('target', help='Existing TVStreammerSAT5 JSON (read only)')
    parser.add_argument('--output', help='Create a NEW private review file; omit for preview only')
    parser.add_argument('--confirm-adapters', action='store_true', help='Confirm Astra adapter/device numbers match the target hardware')
    parser.add_argument('--lof1', type=int, help='Confirmed LNB LOF1 in MHz')
    parser.add_argument('--lof2', type=int, help='Confirmed LNB LOF2 in MHz')
    parser.add_argument('--slof', type=int, help='Confirmed LNB switching frequency in MHz')
    parser.add_argument('--diseqc', action='append', default=[], metavar='TUNER_ID=SOURCE', help='Explicit target DiSEqC source (-1..7) for a tuner')
    parser.add_argument('--base-port', type=int, default=20000, help='First placeholder UDP loopback port (default 20000)')
    args = parser.parse_args()
    if not 1 <= args.base_port <= 65535:
        parser.error('base-port must be 1..65535')
    for value in (args.lof1, args.lof2, args.slof):
        if value is not None and not 1 <= value <= 14000:
            parser.error('LNB frequencies must be 1..14000 MHz')
    args.diseqc_map = {}
    for item in args.diseqc:
        key, sep, value = item.partition('=')
        if not sep or not SAFE_ID.fullmatch(key) or not re.fullmatch(r'-?\d+', value) or not -1 <= int(value) <= 7 or key in args.diseqc_map:
            parser.error('invalid or repeated --diseqc mapping')
        args.diseqc_map[key] = int(value)
    try:
        source = load_json(args.astra)
        target = load_json(args.target)
        result, report = make_plan(source, target, args)
        if args.output:
            if not report['importable']:
                raise ValueError('No new channels to stage')
            blockers = ('dvb_needs_adapter_and_lnb_confirmation', 'dvb_needs_diseqc_mapping',
                        'invalid_or_missing_tuner', 'invalid_tuning', 'invalid_id_or_name',
                        'unsupported_inputs', 'invalid_record', 'no_free_placeholder_port')
            unresolved = [f'{key}={report[key]}' for key in blockers if report[key]]
            if unresolved:
                raise ValueError('Cannot create an incomplete review file: ' + ', '.join(unresolved))
            # O_EXCL also prevents overwriting either source file.
            write_new(args.output, result)
        print('Preview of channels (no settings or credentials imported):')
        for key, value in sorted(report.items()):
            print(f'  {key}: {value}')
        print('All imported channels are stopped; outputs are loopback placeholders; CAM/EPG/outputs require manual configuration.')
        print('The running service was not modified.' + (' Review file created with mode 0600.' if args.output else ' No file was written.'))
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print('Import preview failed: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
