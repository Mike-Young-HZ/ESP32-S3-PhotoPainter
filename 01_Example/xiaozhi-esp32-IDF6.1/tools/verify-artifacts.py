"""Check actual linked board, dependency isolation and image partition budgets."""
from pathlib import Path
import hashlib
import json
from elftools.elf.elffile import ELFFile

root=Path(__file__).resolve().parents[1]
build=root/'build-idf61'
description=json.loads((build/'project_description.json').read_text(encoding='utf-8'))
assert description['project_version']=='2.5.2' and description['target']=='esp32s3'
upstream=root/'migration/upstream-v2.5.2'
for name in ['main/application.cc','main/application.h','main/audio/audio_service.cc',
    'main/device_state_machine.cc','main/mcp_server.cc','main/protocols/mqtt_protocol.cc',
    'main/protocols/websocket_protocol.cc']:
    assert (root/name).read_bytes()==(upstream/name).read_bytes(),f'Unexpected upstream core divergence: {name}'
legacy=(root/'managed_components').as_posix().lower()+'/'
assert not any((p.replace('\\','/').lower()+'/').startswith(legacy) for p in description['build_component_paths'])
commands=json.loads((build/'compile_commands.json').read_text(encoding='utf-8'))
boards=[item['file'] for item in commands if '/main/boards/' in item['file'].replace('\\','/')
    and '/boards/common/' not in item['file'].replace('\\','/')]
assert len(boards)==1 and 'waveshare-s3-PhotoPainter' in boards[0],boards
with (build/'xiaozhi.elf').open('rb') as stream:
    elf=ELFFile(stream)
    factories=[symbol.name for symbol in elf.get_section_by_name('.symtab').iter_symbols()
        if symbol.name=='_Z12create_boardv']
    assert len(factories)==1,factories
files={}
for name in ['xiaozhi.bin','generated_assets.bin','bootloader/bootloader.bin','partition_table/partition-table.bin']:
    data=(build/name).read_bytes()
    files[name]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
assert files['xiaozhi.bin']['bytes']<=0x3f0000
assert files['generated_assets.bin']['bytes']<=0x800000
report={'project':'xiaozhi','version':description['project_version'],'target':description['target'],
    'idf_path':description['idf_path'],'board_sources':boards,'board_factory_count':len(factories),
    'files':files,'app_partition_size':0x3f0000,'app_free_bytes':0x3f0000-files['xiaozhi.bin']['bytes']}
(root/'migration/artifacts.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(f"PASS: one PhotoPainter board factory; isolated dependencies; app {files['xiaozhi.bin']['bytes']} bytes, free {report['app_free_bytes']} bytes")
