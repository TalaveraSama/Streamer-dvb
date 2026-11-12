#!/usr/bin/env python3
"""Preview or stage a conservative channels-only Flussonic import.

This never executes config commands, changes the input files, or starts services.
"""
import argparse
import copy
import ipaddress
import json
import re
import shlex
import sys
from collections import Counter
from urllib.parse import urlsplit
from import_astra_channels import SAFE_ID, load_json, report_label, write_new

MAX_BYTES = 64 * 1024 * 1024
IFACE = re.compile(r'^[A-Za-z0-9_.:-]{1,32}$')


def tokens_from(text):
    # Full-line comments are allowed; URLs containing # are not truncated.
    text = '\n'.join('' if line.lstrip().startswith('#') else line for line in text.splitlines())
    lexer = shlex.shlex(text, posix=True, punctuation_chars='{};')
    lexer.whitespace_split = True
    lexer.commenters = ''
    return list(lexer)


def parse_streams(text):
    tokens = tokens_from(text)
    streams = []
    i = 0
    while i < len(tokens):
        key = tokens[i]; i += 1
        if key in (';', '{', '}'):
            raise ValueError('Invalid top-level syntax')
        values = []
        while i < len(tokens) and tokens[i] not in (';', '{', '}'):
            values.append(tokens[i]); i += 1
        if i >= len(tokens) or tokens[i] == '}':
            raise ValueError('Unterminated top-level directive')
        delim = tokens[i]; i += 1
        if delim == ';':
            if key.lower() in ('include', 'include_file'):
                raise ValueError('External includes require manual review')
            if key == 'stream':
                raise ValueError('Stream directive requires a block')
            continue  # Never retain global settings or credentials.
        if key != 'stream' or len(values) != 1:
            raise ValueError('Unsupported top-level block')
        directives = []
        closed = False
        while i < len(tokens):
            if tokens[i] == '}':
                i += 1; closed = True; break
            name = tokens[i]; i += 1
            vals = []
            while i < len(tokens) and tokens[i] not in (';', '{', '}'):
                vals.append(tokens[i]); i += 1
            if i >= len(tokens) or tokens[i] != ';' or name in (';', '{', '}'):
                raise ValueError('Invalid or nested stream directive')
            i += 1
            directives.append((name, vals))
        if not closed:
            raise ValueError('Unterminated stream block')
        streams.append((values[0], directives))
    return streams


def safe_url(value, allow_query=False):
    try:
        p = urlsplit(value)
        if (p.scheme.lower() not in ('http', 'https') or not p.hostname
                or p.username is not None or p.password is not None or p.fragment
                or (p.query and not allow_query)
                or any(ord(c) < 33 or c in '<>\"\'`' for c in value)):
            return False
        return p.port is None or 1 <= p.port <= 65535
    except ValueError:
        return False


def translate_input(value, opts):
    try:
        parsed = urlsplit(value)
    except ValueError:
        raise ValueError('input_needs_review') from None
    scheme = parsed.scheme.lower()
    if scheme == 'm4f':
        if (not parsed.hostname or parsed.username is not None or parsed.password is not None
                or parsed.query or parsed.fragment):
            raise ValueError('unsafe_m4f')
        # Accessing port validates its range even when the port is omitted.
        try:
            port = parsed.port
        except ValueError:
            raise ValueError('unsafe_m4f') from None
        path = parsed.path.strip('/')
        if not path or not re.fullmatch(r'[A-Za-z0-9_./%-]+', path):
            raise ValueError('unsafe_m4f')
        if opts.m4f_template:
            if port is None:
                raise ValueError('m4f_template_needs_port')
            uri = opts.m4f_template.replace('{host}', parsed.hostname).replace('{port}', str(port)).replace('{path}', path)
        else:
            # The user confirmed this Flussonic endpoint convention. Preserve
            # the source host and port; stream names and source paths may differ.
            uri = 'http://' + parsed.netloc + '/' + path + '/index.m3u8'
        if '{' in uri or '}' in uri or not safe_url(uri, opts.allow_query):
            raise ValueError('invalid_m4f_template')
        return uri, 'hls' if urlsplit(uri).path.lower().endswith('.m3u8') else 'auto'
    aliases = {'hls':('http','hls'), 'hlss':('https','hls'), 'tshttp':('http','http-ts')}
    if scheme in aliases:
        translated, mode = aliases[scheme]
        uri = translated + value[len(scheme):]
    elif scheme in ('http','https'):
        uri = value
        mode = 'hls' if '.m3u8' in parsed.path.lower() else 'auto'
    else:
        raise ValueError('unsupported_input')
    if not safe_url(uri, opts.allow_query):
        raise ValueError('input_needs_review')
    return uri, mode


def udp_output(value, mapping):
    try:
        p = urlsplit(value)
    except ValueError:
        raise ValueError('invalid_udp') from None
    if p.scheme.lower() != 'udp' or not p.hostname or p.path or p.query or p.fragment or p.password is not None:
        raise ValueError('unsupported_push')
    try:
        host = str(ipaddress.IPv4Address(p.hostname))
        port = p.port
    except ValueError:
        raise ValueError('invalid_udp') from None
    if not port or not 1 <= port <= 65535 or host == '0.0.0.0':
        raise ValueError('invalid_udp')
    interface = p.username or ''
    if interface:
        if not IFACE.fullmatch(interface) or interface not in mapping:
            raise ValueError('interface_needs_mapping')
        interface = mapping[interface]
    return {'output_type':'udp-vbr', 'output_mode':'listener', 'output_host':host,
            'output_port':port, 'interface_address':interface}


def make_plan(streams, target, opts):
    existing = target.get('streams')
    if not isinstance(existing, list):
        raise ValueError('Target must contain a streams array')
    used_ids = {x.get('id') for x in existing if isinstance(x, dict)}
    endpoints = set()
    for x in existing:
        if not isinstance(x, dict):
            continue
        outputs = [x]
        for key in ('additional_outputs', 'outputs'):
            if isinstance(x.get(key), list): outputs.extend(x[key])
        for output in outputs:
            if isinstance(output, dict) and str(output.get('output_type', 'udp-cbr')).lower() in ('udp','udp-vbr','udp-cbr'):
                endpoints.add((output.get('output_host'), output.get('output_port')))
    planned = []
    report = Counter()
    def skipped(stream_id, reason):
        report[reason] += 1
        if opts.details:
            label = stream_id if SAFE_ID.fullmatch(stream_id) else '[id no válido]'
            print(f'Pendiente: {label} — {reason} ({report_label(reason)})')
    next_port = opts.base_port
    for sid, directives in streams:
        report['total'] += 1
        if not SAFE_ID.fullmatch(sid):
            skipped(sid, 'invalid_id'); continue
        if sid in used_ids:
            skipped(sid, 'duplicate_id'); continue
        fields = {}
        for key, values in directives:
            fields.setdefault(key, []).append(values)
        if any(key in fields and len(fields[key]) != 1 for key in ('title','provider','disabled')):
            skipped(sid, 'ambiguous_fields'); continue
        title = fields.get('title', [[sid]])[0]
        if len(title) != 1 or not title[0] or len(title[0]) > 200 or any(ord(c) < 32 or c in '<>\"\'`' for c in title[0]):
            skipped(sid, 'invalid_name'); continue
        provider = fields.get('provider', [['']])[0]
        if len(provider) != 1 or len(provider[0]) > 200 or any(ord(c) < 32 or c in '<>\"\'`' for c in provider[0]):
            skipped(sid, 'invalid_provider'); continue
        if 'meta' in fields:
            report['meta_not_converted'] += len(fields['meta'])
            if not opts.omit_meta:
                skipped(sid, 'meta_needs_review'); continue
        disabled = fields.get('disabled', [['false']])[0]
        if disabled not in ([], ['true'], ['false']):
            skipped(sid, 'invalid_disabled'); continue
        if disabled != ['false']: report['source_disabled'] += 1
        inputs = fields.get('input', [])
        if opts.local_hls_base:
            # Explicit relay mode: use the local Flussonic stream ID, not its
            # upstream input URL. Upstream failover stays in Flussonic.
            translated = [(opts.local_hls_base + '/' + sid + '/index.m3u8', 'hls')]
        else:
            if not 1 <= len(inputs) <= 2 or any(len(item) != 1 for item in inputs):
                skipped(sid, 'unsupported_input_count'); continue
            try:
                translated = [translate_input(item[0], opts) for item in inputs]
            except ValueError as error:
                skipped(sid, str(error)); continue
            # Flussonic failover semantics may differ; require an explicit review
            # before saving a stream with multiple inputs.
            if len(translated) > 1 and not opts.confirm_backup_order:
                skipped(sid, 'backup_order_needs_confirmation'); continue
            if len(translated) > 1 and translated[0][1] != translated[1][1]:
                skipped(sid, 'incompatible_backup_modes'); continue
        outputs = []
        invalid = False
        for push in fields.get('push', []):
            if len(push) != 1:
                skipped(sid, 'invalid_udp'); invalid = True; break
            if not push[0].lower().startswith('udp://'):
                report['non_udp_push_omitted'] += 1
                continue
            try:
                outputs.append(udp_output(push[0], opts.interfaces))
            except ValueError as error:
                skipped(sid, str(error)); invalid = True; break
        if invalid: continue
        if not outputs:
            while ('127.0.0.1', next_port) in endpoints and next_port <= 65535: next_port += 1
            if next_port > 65535:
                skipped(sid, 'no_free_placeholder_port'); continue
            outputs = [{'output_type':'udp-vbr', 'output_mode':'listener', 'output_host':'127.0.0.1',
                        'output_port':next_port, 'interface_address':''}]
            next_port += 1
            report['no_udp_placeholder'] += 1
        duplicates = sum((out['output_host'], out['output_port']) in endpoints for out in outputs)
        # Also flag repeated UDP outputs within the same stream.
        duplicates += len(outputs) - len({(out['output_host'], out['output_port']) for out in outputs})
        if duplicates: report['udp_endpoint_conflicts'] += duplicates
        for out in outputs: endpoints.add((out['output_host'],out['output_port']))
        entry = {'id':sid, 'name':title[0], 'service_name':title[0], 'service_provider':provider[0],
                 'input_uri':translated[0][0], 'input_mode':translated[0][1],
                 'auto_start':False, 'input_service_id':0, 'service_id':1,
                 'remap_enabled':False, 'transcode_enabled':False, 'cbr':False,
                 **outputs[0], 'additional_outputs':outputs[1:]}
        if len(translated) == 2:
            entry['backup_input_uri'] = translated[1][0]
            entry['backup_input_type'] = 'url'
        for key in fields:
            if key not in ('title','provider','disabled','input','push','meta'):
                report['unconverted_directives'] += len(fields[key])
        planned.append(entry); used_ids.add(sid); report['importable'] += 1
        if opts.local_hls_base:
            report['local_hls_inputs'] += 1
            if len(inputs) > 1: report['local_hls_upstream_backups_not_copied'] += 1
            if not inputs: report['local_hls_no_input_directive'] += 1
    result = copy.deepcopy(target)
    result['streams'].extend(planned)
    return result, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('flussonic', help='Flussonic config file (read only)')
    parser.add_argument('target', help='Existing TVStreammerSAT5 JSON (read only)')
    parser.add_argument('--output', help='Create a NEW private review file; omit for preview only')
    parser.add_argument('--interface', action='append', default=[], metavar='OLD=NEW', help='Confirmed interface name or IPv4 address on the target')
    parser.add_argument('--local-hls-base', help='Use local Flussonic HTTP(S) origin and stream ID for all inputs, e.g. http://192.0.2.7:2086')
    parser.add_argument('--m4f-template', help='Optional verified HTTP URL template containing {host}, {port} and {path}; default preserves host/path and appends /index.m3u8')
    parser.add_argument('--allow-query', action='store_true', help='Allow URL query strings, which may contain secrets')
    parser.add_argument('--confirm-backup-order', action='store_true', help='Treat first input as primary and second as backup')
    parser.add_argument('--omit-meta', action='store_true', help='After review, omit Flussonic meta directives')
    parser.add_argument('--details', action='store_true', help='Show IDs and reasons for channels needing review')
    parser.add_argument('--allow-partial', action='store_true', help='Explicitly stage only supported, resolved channels')
    parser.add_argument('--allow-duplicate-udp', action='store_true', help='Stage duplicate UDP destinations, with all channels stopped')
    parser.add_argument('--base-port', type=int, default=20000)
    args = parser.parse_args()
    if not 1 <= args.base_port <= 65535: parser.error('Invalid base port')
    args.interfaces = {}
    for mapping in args.interface:
        old, sep, new = mapping.partition('=')
        if not sep or not IFACE.fullmatch(old) or not IFACE.fullmatch(new) or old in args.interfaces:
            parser.error('Invalid or repeated interface mapping')
        args.interfaces[old] = new
    if args.local_hls_base:
        if args.m4f_template or args.allow_query or args.confirm_backup_order:
            parser.error('--local-hls-base cannot be combined with input translation options')
        try:
            base = urlsplit(args.local_hls_base)
            if (not safe_url(args.local_hls_base) or base.path not in ('', '/')
                    or base.query or base.fragment):
                raise ValueError()
        except ValueError:
            parser.error('--local-hls-base must be an HTTP(S) origin without credentials, path, query or fragment')
        args.local_hls_base = args.local_hls_base.rstrip('/')
    if args.m4f_template and not all(x in args.m4f_template for x in ('{host}', '{port}', '{path}')):
        parser.error('The m4f template must contain {host}, {port} and {path}')
    try:
        with open(args.flussonic, 'rb') as f: raw = f.read(MAX_BYTES + 1)
        if len(raw) > MAX_BYTES: raise ValueError('Config exceeds 64 MiB')
        streams = parse_streams(raw.decode('utf-8-sig'))
        result, report = make_plan(streams, load_json(args.target), args)
        for key, value in sorted(report.items()): print(f'{key}: {value} ({report_label(key)})')
        print('Todos los canales quedan detenidos; revisa destinos, entradas y opciones no convertidas antes de iniciarlos.')
        if args.local_hls_base:
            print('Aviso: entradas locales HLS por ID; no se copian las entradas originales ni sus respaldos. Comprueba que Flussonic sirva cada stream.')
        if args.allow_query:
            print('Aviso: las URL con consultas pueden incluir secretos y se guardan en la configuración.')
        blockers = ('invalid_id','ambiguous_fields','invalid_name','invalid_provider','invalid_disabled','meta_needs_review','unsupported_input_count',
                    'unsafe_m4f','m4f_template_needs_port','invalid_m4f_template','unsupported_input',
                    'input_needs_review','backup_order_needs_confirmation','incompatible_backup_modes','interface_needs_mapping','invalid_udp',
                    'unsupported_push','no_free_placeholder_port')
        if args.output:
            if not report['importable']: raise ValueError('No new streams to stage')
            unresolved = [f'{k}={report[k]}' for k in blockers if report[k]]
            if unresolved and not args.allow_partial:
                raise ValueError('Cannot create an incomplete review file: ' + ', '.join(unresolved))
            if report['udp_endpoint_conflicts'] and not args.allow_duplicate_udp:
                raise ValueError(f"UDP destinations conflict: udp_endpoint_conflicts={report['udp_endpoint_conflicts']}")
            if unresolved:
                print('WARNING: partial review file; copia parcial: revisa los canales omitidos.')
            write_new(args.output, result)
            print('Copia privada creada. El servicio no se modificó.')
        else:
            print('Solo vista previa; no se escribió ningún archivo.')
    except (OSError, ValueError, UnicodeDecodeError, json.JSONDecodeError) as error:
        print('Import preview failed: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
