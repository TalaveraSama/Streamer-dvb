#!/usr/bin/env python3
"""Vista previa unificada de canales Astra JSON o Flussonic desde config.txt.

Detecta el contenido, no la extensión. Nunca aplica la configuración activa.
"""
import argparse
import json
import os
import sys
from pathlib import Path

MAX_BYTES = 64 * 1024 * 1024


def detect_format(path):
    with open(path, 'rb') as source:
        raw = source.read(MAX_BYTES + 1)
    if len(raw) > MAX_BYTES:
        raise ValueError('El archivo supera 64 MiB')
    text = raw.decode('utf-8-sig')
    if not text.strip():
        raise ValueError('El archivo está vacío')
    if text.lstrip().startswith('{'):
        try:
            data = json.loads(text)
        except json.JSONDecodeError:
            raise ValueError('El JSON de Astra no es válido') from None
        if not isinstance(data, dict) or not isinstance(data.get('make_stream'), list):
            raise ValueError('El JSON no contiene una lista make_stream de Astra')
        return 'astra'
    # Parse, do not execute or evaluate configuration text. Reject unknown or
    # empty documents instead of silently interpreting them as Flussonic.
    from import_flussonic_channels import parse_streams
    try:
        streams = parse_streams(text)
    except ValueError:
        raise ValueError('La sintaxis de Flussonic no es compatible o está incompleta') from None
    if not streams:
        raise ValueError('No se encontraron bloques stream de Flussonic')
    return 'flussonic'


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog=('Opciones adicionales: el importador detectado las valida. '\
                'Consulta docs/IMPORTAR_CONFIG.md y la ayuda del importador correspondiente.'))
    parser.add_argument('config', help='config.txt de Astra (JSON) o Flussonic (texto)')
    parser.add_argument('destino', help='configuración JSON existente de TVStreammerSAT5, solo lectura')
    args, options = parser.parse_known_args()
    try:
        detected = detect_format(args.config)
        script = Path(__file__).resolve().parent / ('import_' + detected + '_channels.py')
        print('Formato detectado: ' + ('Astra/Cesbo' if detected == 'astra' else 'Flussonic'), flush=True)
        os.execv(sys.executable, [sys.executable, str(script), args.config, args.destino, *options])
    except (OSError, ValueError, UnicodeDecodeError) as error:
        print('No se pudo analizar la configuración: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
