project('chez-scheme', 'c',
    version: '10.1.0',
    license: 'Apache-2.0',
)

chez_src = meson.current_source_dir()

# Pre-generated headers for portable bytecode backend
chez_boot_inc = include_directories('boot/pb')

# C source headers
chez_c_inc = include_directories('c')

# Kernel source files (from c/build.zuo kernel-src-files)
# pb backend uses pb.c instead of i3le.c/clearcache.c
# Windows adds windows.c
kernel_src = files(
    'c/statics.c',
    'c/segment.c',
    'c/alloc.c',
    'c/symbol.c',
    'c/intern.c',
    'c/gcwrapper.c',
    'c/gc-011.c',
    'c/gc-par.c',
    'c/gc-ocd.c',
    'c/gc-oce.c',
    'c/number.c',
    'c/schsig.c',
    'c/io.c',
    'c/new-io.c',
    'c/print.c',
    'c/fasl.c',
    'c/vfasl.c',
    'c/stats.c',
    'c/foreign.c',
    'c/prim.c',
    'c/prim5.c',
    'c/flushcache.c',
    'c/schlib.c',
    'c/thread.c',
    'c/expeditor.c',
    'c/scheme.c',
    'c/compress-io.c',
    'c/random.c',
    'c/ffi.c',
    'c/self-exe.c',
    'c/pb.c',
    'c/windows.c',
)

# Compile defines for pb backend on Windows
chez_c_args = [
    '-DPORTABLE_BYTECODE',
    '-DFEATURE_WINDOWS',
    '-DWIN32',
    '-DSCHEME_STATIC',
    '-D_CRT_SECURE_NO_WARNINGS',
    '-DWIN32_LEAN_AND_MEAN',
    '-I' + meson.current_source_dir() / 'boot' / 'pb',
    '-I' + meson.current_source_dir() / 'c',
    '-I' + meson.current_source_dir() / 'zlib',
    '-I' + meson.current_source_dir() / 'lz4' / 'lib',
]

chez_kernel_lib = static_library('chezkernel',
    kernel_src,
    c_args: chez_c_args,
    include_directories: [chez_boot_inc, chez_c_inc],
)

chez_scheme_dep = declare_dependency(
    include_directories: [chez_boot_inc, chez_c_inc],
    link_with: chez_kernel_lib,
)
