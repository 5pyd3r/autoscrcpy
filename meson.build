project('autoscrcpy', 'c',
    version: '1.0.0',
    meson_version: '>=1.3.0',
    default_options: [
        'c_std=c11',
        'buildtype=debugoptimized',
        'warning_level=2',
    ]
)

# Windows-specific definitions
if host_machine.system() == 'windows'
    add_project_arguments('-DUNICODE', '-D_UNICODE', '-D_CRT_SECURE_NO_WARNINGS', '-DWIN32_LEAN_AND_MEAN', language: 'c')
endif

# Dependencies
libavcodec_dep = dependency('libavcodec', required: true)
libavformat_dep = dependency('libavformat', required: true)
libavutil_dep = dependency('libavutil', required: true)
libswscale_dep = dependency('libswscale', required: true)
libswresample_dep = dependency('libswresample', required: true)
mbedtls_dep = dependency('mbedtls', required: true)
chez_scheme_dep = dependency('chez-scheme', required: false,
    fallback: ['chez-scheme', 'chez_scheme_dep'])

# Windows libraries
winlibs = []
if host_machine.system() == 'windows'
    cc = meson.get_compiler('c')
    winlibs = [
        cc.find_library('d3d11'),
        cc.find_library('dxgi'),
        cc.find_library('user32'),
        cc.find_library('kernel32'),
        cc.find_library('gdi32'),
        cc.find_library('ole32'),
        cc.find_library('oleaut32'),
        cc.find_library('ws2_32'),
        cc.find_library('imm32'),
        cc.find_library('bcrypt'),
        cc.find_library('uuid'),
        cc.find_library('rpcrt4'),
        cc.find_library('mmdevapi'),
        cc.find_library('comctl32'),
    ]
endif

subdir('src')
subdir('tests')
