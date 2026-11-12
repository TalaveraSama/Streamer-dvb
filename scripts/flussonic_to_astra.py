#!/usr/bin/env python3
"""Prepare a private, separate Astra review copy from Flussonic; never install it."""
import argparse
import copy
import ipaddress
import json
import re
import sys
from collections import Counter, defaultdict
from urllib.parse import urlsplit

from import_astra_channels import SAFE_ID, load_json, write_new
from import_flussonic_channels import MAX_BYTES, IFACE, parse_streams, safe_url, udp_output


def base36(number):
    digits = '0123456789abcdefghijklmnopqrstuvwxyz'
    if not number:
        return '0'
    out = ''
    while number:
        number, remainder = divmod(number, 36)
        out = digits[remainder] + out
    return out


def all_ids(value):
    if isinstance(value, dict):
        if isinstance(value.get('id'), str):
            yield value['id']
        for item in value.values():
            yield from all_ids(item)
    elif isinstance(value, list):
        for item in value:
            yield from all_ids(item)


def input_key(value):
    if isinstance(value, dict):
        value = value.get('url')
    if not isinstance(value, str):
        return None
    # Some exports pasted into Markdown are transformed into [url](url).
    match = re.fullmatch(r'\[([^\]]+)\]\(([^)]+)\)', value)
    if match and match.group(1) == match.group(2):
        value = match.group(1)
    try:
        parsed = urlsplit(value)
        if parsed.scheme.lower() not in ('http', 'https') or not parsed.hostname:
            return None
        port = parsed.port or (443 if parsed.scheme.lower() == 'https' else 80)
        return (parsed.scheme.lower(), parsed.hostname.lower(), port, parsed.path)
    except ValueError:
        return None


def existing_udp(value):
    if isinstance(value, dict):
        value = value.get('url')
    if not isinstance(value, str):
        raise ValueError('Salida Astra no reconocida: revisa el archivo de origen')
    if not re.match(r'^(udp|rtp)://', value, re.I):
        return None
    try:
        parsed = urlsplit(value)
        address = str(ipaddress.IPv4Address(parsed.hostname))
        port = parsed.port or 1234  # Astra UDP default.
        if not (1 <= port <= 65535) or address == '0.0.0.0' or parsed.password is not None:
            raise ValueError()
        return (address, port)
    except (TypeError, ValueError):
        raise ValueError('Salida UDP/RTP existente no reconocida: revisión manual necesaria') from None


def safe_id_label(value):
    return value if isinstance(value, str) and SAFE_ID.fullmatch(value) else '[id inválido]'


def plan(source, target, base, old_iface, new_iface):
    streams = target.get('make_stream', [])
    if not isinstance(streams, list) or any(not isinstance(s, dict) for s in streams):
        raise ValueError('make_stream debe ser un arreglo de objetos')
    gid = target.get('gid')
    if isinstance(gid, bool) or not isinstance(gid, int) or not 0 <= gid < 2**53 - 100000:
        raise ValueError('El export de Astra debe tener un gid numérico válido')
    used_ids = set(all_ids(target))
    existing_inputs = set()
    existing_names = set()
    endpoints = defaultdict(list)
    for stream in streams:
        for field in ('input', 'output'):
            if not isinstance(stream.get(field, []), list):
                raise ValueError(f'El campo {field} de un stream Astra no es un arreglo')
        existing_names.add(stream.get('name')) if isinstance(stream.get('name'), str) else None
        for item in stream.get('input', []):
            key = input_key(item)
            if key: existing_inputs.add(key)
        for item in stream.get('output', []):
            endpoint = existing_udp(item)
            if endpoint:
                endpoints[endpoint].append(('existente', safe_id_label(stream.get('id'))))
    result = copy.deepcopy(target)
    result.setdefault('make_stream', [])
    report = Counter()
    issues = []
    conflicts = []
    source_ids = set()
    for sid, directives in source:
        report['total'] += 1
        def issue(reason):
            report[reason] += 1
            issues.append((safe_id_label(sid), reason))
        if not SAFE_ID.fullmatch(sid):
            issue('id_invalido'); continue
        if sid in source_ids:
            issue('id_duplicado_en_flussonic'); continue
        source_ids.add(sid)
        fields = defaultdict(list)
        for name, vals in directives:
            fields[name].append(vals)
        if any(len(fields.get(name, [])) > 1 for name in ('title', 'provider', 'disabled')):
            issue('campos_ambiguos'); continue
        title = fields.get('title', [[sid]])[0]
        if len(title) != 1 or not title[0] or len(title[0]) > 200 or any(ord(c) < 32 for c in title[0]):
            issue('nombre_invalido'); continue
        disabled = fields.get('disabled', [['false']])[0]
        if disabled not in ([], ['true'], ['false']):
            issue('estado_invalido'); continue
        if disabled != ['false']:
            report['deshabilitados_en_flussonic'] += 1
        uri = base + '/' + sid + '/index.m3u8'
        if input_key(uri) in existing_inputs:
            report['entrada_ya_en_astra'] += 1
            issues.append((sid, 'entrada_ya_en_astra (omitido, no modificado)'))
            continue
        if title[0] in existing_names:
            issue('nombre_ya_en_astra_revisar'); continue
        outputs = []
        invalid = False
        for push in fields.get('push', []):
            if len(push) != 1:
                issue('push_invalido'); invalid = True; break
            if not push[0].lower().startswith('udp://'):
                report['push_no_udp_omitidos'] += 1
                continue
            try:
                parsed = udp_output(push[0], {old_iface: new_iface})
            except ValueError as exc:
                issue(str(exc)); invalid = True; break
            iface = parsed['interface_address'] or new_iface
            outputs.append('udp://' + iface + '@' + parsed['output_host'] + ':' + str(parsed['output_port']))
        if invalid: continue
        report['meta_omitidos'] += len(fields.get('meta', []))
        for key, values in fields.items():
            if key not in ('title', 'provider', 'disabled', 'input', 'push', 'meta'):
                report['otras_directivas_no_convertidas'] += len(values)
        if len(fields.get('input', [])) > 1:
            report['respaldos_quedan_en_flussonic'] += 1
        if not outputs:
            report['sin_salida_udp'] += 1
        for out in outputs:
            endpoint = existing_udp(out)
            if endpoints[endpoint]:
                report['conflictos_udp'] += 1
                conflicts.append((endpoint, sid, list(endpoints[endpoint])))
            endpoints[endpoint].append(('nuevo', sid))
        while True:
            gid += 1
            new_id = base36(gid)
            if new_id not in used_ids:
                break
        used_ids.add(new_id)
        result['make_stream'].append({'id':new_id, 'name':title[0], 'type':'spts',
                                      'enable':False, 'input':[uri], 'output':outputs})
        existing_inputs.add(input_key(uri))
        report['preparados'] += 1
    result['gid'] = gid
    return result, report, issues, conflicts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('flussonic', help='Archivo Flussonic, solo lectura')
    parser.add_argument('astra', help='Export JSON completo de Astra, solo lectura')
    parser.add_argument('--local-hls-base', required=True, help='Origen HTTP(S) de Flussonic, sin ruta ni credenciales')
    parser.add_argument('--interface', required=True, metavar='ORIGEN=DESTINO')
    parser.add_argument('--output', help='Crear copia NUEVA privada; sin esta opción solo vista previa')
    parser.add_argument('--allow-duplicate-udp', action='store_true', help='Aceptar duplicados tras revisarlos; canales nuevos detenidos')
    parser.add_argument('--details', action='store_true', help='Mostrar IDs omitidos y destinos UDP duplicados')
    args = parser.parse_args()
    old, sep, new = args.interface.partition('=')
    if not sep or not IFACE.fullmatch(old) or not IFACE.fullmatch(new):
        parser.error('Interfaz inválida; usar ORIGEN=DESTINO')
    try:
        parsed = urlsplit(args.local_hls_base)
        if not safe_url(args.local_hls_base) or parsed.path not in ('', '/'):
            raise ValueError('Origen HLS inválido: usa http(s)://host:puerto sin ruta, consulta ni credenciales')
        with open(args.flussonic, 'rb') as f:
            data = f.read(MAX_BYTES + 1)
        if len(data) > MAX_BYTES: raise ValueError('Flussonic supera 64 MiB')
        source = parse_streams(data.decode('utf-8-sig'))
        result, report, issues, conflicts = plan(source, load_json(args.astra), args.local_hls_base.rstrip('/'), old, new)
        for key in ('total', 'preparados', 'entrada_ya_en_astra', 'sin_salida_udp', 'conflictos_udp', 'deshabilitados_en_flussonic', 'meta_omitidos', 'push_no_udp_omitidos', 'otras_directivas_no_convertidas', 'respaldos_quedan_en_flussonic'):
            print(f'{key}: {report[key]}')
        for key in sorted(report.keys() - {'total', 'preparados', 'entrada_ya_en_astra', 'sin_salida_udp', 'conflictos_udp', 'deshabilitados_en_flussonic', 'meta_omitidos', 'push_no_udp_omitidos', 'otras_directivas_no_convertidas', 'respaldos_quedan_en_flussonic'}):
            print(f'{key}: {report[key]}')
        if args.details:
            for sid, reason in issues: print(f'Revisión: {sid} — {reason}')
            for (host, port), sid, previous in conflicts:
                print(f'UDP duplicado: {host}:{port} — nuevo {sid}; ya usado por ' + ', '.join(f'{kind} {ident}' for kind, ident in previous))
        print('Nuevos canales detenidos; los originales, ajustes y servicios no se modifican.')
        print('Las fuentes y respaldos originales quedan en Flussonic; verifica cada HLS y la red multicast.')
        if args.output:
            unresolved = [(sid, reason) for sid, reason in issues if not reason.startswith('entrada_ya_en_astra')]
            if unresolved: raise ValueError('Hay canales pendientes; no se generó copia parcial. Usa --details.')
            if not report['preparados']: raise ValueError('No hay canales nuevos para generar')
            if report['conflictos_udp'] and not args.allow_duplicate_udp:
                raise ValueError('Destinos UDP duplicados: revisa --details y confirma con --allow-duplicate-udp')
            write_new(args.output, result)
            print('Copia privada creada; NO se ha instalado ni iniciado.')
        else:
            print('Solo vista previa; no se creó ningún archivo.')
    except (OSError, ValueError, TypeError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        print('Error: ' + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
