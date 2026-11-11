# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['mainApp.py'],
    pathex=[],
    binaries=[('/home/vitor/projetos/TCC/Codigos/SIBA/build_native_sim/zephyr/zephyr.exe', 'native_sim_bin/SIBA'), ('/home/vitor/projetos/TCC/Codigos/SAME/build_native_sim/zephyr/zephyr.exe', 'native_sim_bin/SAME')],
    datas=[('Config', 'Config'), ('Scripts', 'Scripts'), ('GUI/mainAlbatrozGUI.ui', 'GUI')],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='AlbatrozGUI',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    contents_directory='.',
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='AlbatrozGUI',
)
