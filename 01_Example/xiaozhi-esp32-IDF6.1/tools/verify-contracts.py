"""Check migration-specific contracts and distinguish preserved business code."""
from pathlib import Path
import hashlib
import json
import re

root=Path(__file__).resolve().parents[1]
source=root.with_name('xiaozhi-esp32')
board='main/boards/waveshare-s3-PhotoPainter/esp32-s3-PhotoPainter.cc'
old=(source/board).read_text(encoding='utf-8')
new=(root/board).read_text(encoding='utf-8')
def tools_body(s):
    return s[s.index('    void InitializeTools()'):s.index('  public:',s.index('    void InitializeTools()'))]
def tool_blocks(s):
    chunks = re.split(r'(?=        mcp_server.AddTool\(")', tools_body(s))[1:]
    return {re.search(r'AddTool\("([^"]+)', chunk).group(1): chunk.strip() for chunk in chunks}
old_tools, new_tools = tool_blocks(old), tool_blocks(new)
assert set(new_tools) == set(old_tools) | {'self.disp.refreshCurrentImage'}
for name in old_tools:
    if name not in {'self.disp.refreshWeather', 'self.disp.SwitchPictures', 'self.disp.aiIMG'}:
        assert old_tools[name] == new_tools[name], name
for name in ['self.disp.refreshWeather', 'self.disp.SwitchPictures', 'self.disp.aiIMG']:
    assert 'queued:' in new_tools[name] and 'return true;' not in new_tools[name], name
assert 'RequestCurrentImageRefresh()' in new_tools['self.disp.refreshCurrentImage']
assert '刷新图片、刷新屏幕、换图片时禁止调用' in new_tools['self.disp.refreshWeather']
assert len(new_tools) == 10
assert 'xiaozhi_application_received(DeviceStateMachine::GetStateName' in new
assert 'xiaozhi_ai_Message(role, content)' in new
config=(root/'sdkconfig').read_text(encoding='utf-8')
for required in ['CONFIG_BOARD_TYPE_ESP32S3_PhotoPaint=y','CONFIG_ESPTOOLPY_FLASHSIZE="16MB"',
    'CONFIG_SPIRAM_MODE_OCT=y','CONFIG_SPIRAM_SPEED_80M=y','CONFIG_USE_DEVICE_AEC=y',
    'CONFIG_LANGUAGE_ZH_CN=y','CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS=y','CONFIG_FLASH_DEFAULT_ASSETS=y']:
    assert required in config,required
assert (root/'partitions/v2/16m.csv').read_bytes()==(source/'partitions/v2/16m.csv').read_bytes()
allowed={'components/user_app_bsp/user_app.h', 'components/user_app_bsp/mode_src/xiaozhi_mode.cpp', 'components/port_bsp/traverse_nvs.cpp', 'components/codec_board/lcd_init.c', 'components/port_bsp/codec_bsp.cpp',
    'components/port_bsp/codec_bsp.h', 'components/user_app_bsp/mode_src/Mode_Selection.cpp'}
comparison=[]
for p in (source/'components').rglob('*'):
    if p.is_file() and p.suffix.lower() in ('.c','.cc','.cpp','.h','.hpp','.tpp'):
        rel=p.relative_to(source).as_posix()
        unchanged=p.read_bytes()==(root/rel).read_bytes()
        assert unchanged or rel in allowed, f'Unreviewed business source change: {rel}'
        comparison.append({'path':rel,'unchanged':unchanged})
(root/'migration/business-source-comparison.json').write_text(json.dumps(comparison,indent=2),encoding='utf-8')
directories=[str(p.relative_to(source)) for p in source.rglob('*') if p.is_dir()]
for rel in directories: assert (root/rel).is_dir(),f'Missing directory: {rel}'
(root/'migration/source-directories.json').write_text(json.dumps(directories,indent=2),encoding='utf-8')
print(f'PASS: MCP bodies, board callbacks, configuration, partition layout, {len(comparison)} business source files and {len(directories)} original directories')
