#!/usr/bin/env python3
"""Unofficial desktop 1.5.2 effects and theme patcher. Read README.md before use.
No third-party Python packages required. The stock input is never overwritten.
"""
import argparse,base64,hashlib,json,os,pathlib,re,sys
MANIFEST=json.loads(__MANIFEST_JSON__)
def accent_bytes(color,alpha=255):
    if not isinstance(color,str) or not re.fullmatch(r'#[0-9a-fA-F]{6}',color):
        raise ValueError('Accent must be #RRGGBB, for example #9455fa')
    return ((alpha<<24)|int(color[1:],16)).to_bytes(4,'little')
def theme_values(theme=None,accent=None):
    if theme is None:theme={}
    if not isinstance(theme,dict):raise ValueError('Theme must be an object of named #RRGGBB colours')
    fields=MANIFEST['theme']['fields'];keys={f['key'] for f in fields}
    legacy=set(MANIFEST['theme'].get('legacy_keys',[]));theme={k:v for k,v in theme.items() if k not in legacy}
    if any(key not in keys for key in theme):raise ValueError('Unknown theme colour key')
    result={f['key']:theme.get(f['key'],f['default']) for f in fields}
    if accent is not None:result['accent']=accent
    for value in result.values():accent_bytes(value)
    return result
def normalize_theme(data):
    if len(data)!=MANIFEST['size']:raise ValueError('Firmware size mismatch')
    output=bytearray(data)
    for field in MANIFEST['theme']['fields']:
        previous=None
        for offset in field['offsets']:
            word=bytes(output[offset:offset+4])
            if len(word)!=4 or word[3]!=field.get('alpha',255) or (previous is not None and word!=previous):
                raise ValueError('Theme palette entries differ or have invalid alpha: '+field['key'])
            previous=word;output[offset:offset+4]=accent_bytes(field['default'],field.get('alpha',255))
    return bytes(output)
# Compatibility for existing accent-only callers and regression tests.
normalize_accent=normalize_theme
def transform(data,restore=False,accent=None,theme=None):
    if restore and (accent is not None or theme is not None):raise ValueError('Theme/accent options apply when patching, not restoring')
    selected=theme_values(theme,accent)
    source=normalize_theme(data) if restore else data
    expected=MANIFEST['patched_sha256' if restore else 'stock_sha256']
    if len(source)!=MANIFEST['size'] or hashlib.sha256(source).hexdigest()!=expected:
        raise ValueError('Firmware mismatch. '+('Expected this Cobalt v'+MANIFEST['version']+' build with valid theme palette entries.' if restore else 'Expected the original desktop 1.5.2 BLUEBOX.BIN. Modified, Eurorack and other versions are refused.'))
    output=bytearray(source)
    for p in MANIFEST['patches']:
        old=base64.b64decode(p['new' if restore else 'old']);new=base64.b64decode(p['old' if restore else 'new']);o=p['offset']
        if len(old)!=len(new) or output[o:o+len(old)]!=old:raise ValueError('Original byte check failed: '+p['name'])
        output[o:o+len(new)]=new
    expected=MANIFEST['stock_sha256' if restore else 'patched_sha256']
    if len(output)!=len(data) or hashlib.sha256(output).hexdigest()!=expected:raise ValueError('Output integrity check failed')
    if not restore:
        for field in MANIFEST['theme']['fields']:
            for offset in field['offsets']:
                if output[offset:offset+4]!=accent_bytes(field['default'],field.get('alpha',255)):raise ValueError('Unexpected palette bytes')
                output[offset:offset+4]=accent_bytes(selected[field['key']],field.get('alpha',255))
        if hashlib.sha256(normalize_theme(output)).hexdigest()!=MANIFEST['patched_sha256']:raise ValueError('Custom output integrity check failed')
    return bytes(output)
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('firmware',type=pathlib.Path)
    ap.add_argument('--output',type=pathlib.Path,help='New output file; existing files are never overwritten')
    ap.add_argument('--accent',help='UI accent as #RRGGBB; default #9455fa. No compilation needed.')
    ap.add_argument('--theme',type=pathlib.Path,help='Theme JSON exported by the patching page; --accent overrides its accent')
    ap.add_argument('--check',action='store_true',help='Validate and simulate without writing a firmware file')
    ap.add_argument('--restore',action='store_true',help='Reconstruct the exact original from this exact patched build')
    a=ap.parse_args()
    try:
        theme=None
        if a.theme:
            payload=json.loads(a.theme.read_text())
            if not isinstance(payload,dict) or set(payload)!={'schema','colors'} or type(payload['schema']) not in (int,float) or payload['schema']!=1:raise ValueError('Unsupported theme file schema')
            theme=payload['colors']
            if not isinstance(theme,dict):raise ValueError('Theme colours must be an object')
        out=transform(a.firmware.read_bytes(),a.restore,a.accent,theme)
        print('Input and all patch locations verified.')
        print('Output SHA256:',hashlib.sha256(out).hexdigest())
        if a.check:
            print('Check only: no firmware file was written.');return 0
        dest=a.output or pathlib.Path('restored' if a.restore else 'modded')/'BLUEBOX.BIN'
        if dest.resolve()==a.firmware.resolve():raise ValueError('Output cannot be the input')
        if dest.exists():raise ValueError('Output already exists; choose a new path: '+str(dest))
        dest.parent.mkdir(parents=True,exist_ok=True)
        # Exclusive create also protects against a race after the exists check.
        with dest.open('xb') as f:
            try:
                f.write(out);f.flush();os.fsync(f.fileno())
            except BaseException:
                f.close();dest.unlink(missing_ok=True);raise
        print('Wrote:',dest)
        print('Unofficial firmware. Keep your original BLUEBOX.BIN and back up your projects before installing.')
        return 0
    except (OSError,ValueError) as e:
        print('ERROR:',e,file=sys.stderr);return 1
if __name__=='__main__':raise SystemExit(main())
