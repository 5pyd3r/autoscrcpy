# Platform sources
platform_src = files(
    'platform/log.c',
    'platform/thread.c',
)

# ADB sources
adb_src = files(
    'adb/adb.c',
    'adb/protocol.c',
    'adb/transport.c',
    'adb/session.c',
    'adb/crypto.c',
    'adb/tls.c',
)

# Device sources
device_src = files(
    'device/server.c',
    'device/video_socket.c',
    'device/audio_socket.c',
    'device/control_socket.c',
    'device/device_msg.c',
    'device/demuxer.c',
)

# Decode sources
decode_src = files(
    'decode/video_decoder.c',
    'decode/audio_decoder.c',
)

# Render sources
render_src = files(
    'render/d3d_context.c',
    'render/video_renderer.c',
    'render/shader.c',
)

# Input sources
input_src = files(
    'input/keycode_map.c',
    'input/input_transform.c',
)

# Control sources
control_src = files(
    'control/control_msg.c',
    'control/clipboard.c',
    'control/power.c',
    'control/controller.c',
)

# Record sources
record_src = files(
    'record/recorder.c',
    'record/muxer.c',
)

# Audio sources
audio_src = files(
    'audio/player.c',
    'audio/regulator.c',
)

# Pipeline sources
pipeline_src = files(
    'pipeline/video_pipeline.c',
    'pipeline/audio_pipeline.c',
)

# App sources
app_src = files(
    'app/application.c',
    'app/window.c',
    'app/options.c',
    'app/cli.c',
    'app/config.c',
)

# Script sources
script_src = files(
    'script/message_queue.c',
    'script/engine.c',
    'script/event_dispatch.c',
    'script/repl_window.c',
    'script/bindings.c',
)

# Copy Chez Scheme boot files and init.ss to build directory (uses Python to avoid sandbox)
chez_boot_dir = meson.current_source_dir() / '..' / 'subprojects' / 'chez-scheme' / 'boot' / 'pb'
custom_target('chez-init-ss',
    output: 'init.ss',
    command: [find_program('python', 'python3'), '-c',
              'import shutil, sys, os; os.makedirs(os.path.dirname(sys.argv[2]), exist_ok=True); shutil.copy2(sys.argv[1], sys.argv[2])',
              meson.current_source_dir() / '..' / 'lib' / 'init.ss', '@OUTPUT@'],
    build_by_default: true,
)
custom_target('chez-scheme-boot',
    output: 'scheme.boot',
    command: [find_program('python', 'python3'), '-c',
              'import shutil, sys; shutil.copy2(sys.argv[1], sys.argv[2])',
              chez_boot_dir / 'scheme.boot', '@OUTPUT@'],
    build_by_default: true,
    install: true,
    install_dir: get_option('bindir'),
)
custom_target('chez-petite-boot',
    output: 'petite.boot',
    command: [find_program('python', 'python3'), '-c',
              'import shutil, sys; shutil.copy2(sys.argv[1], sys.argv[2])',
              chez_boot_dir / 'petite.boot', '@OUTPUT@'],
    build_by_default: true,
    install: true,
    install_dir: get_option('bindir'),
)

# Main executable
executable('autoscrcpy',
    'main.c',
    platform_src,
    adb_src,
    device_src,
    decode_src,
    render_src,
    input_src,
    control_src,
    record_src,
    audio_src,
    pipeline_src,
    app_src,
    script_src,
    dependencies: [libavcodec_dep, libavformat_dep, libavutil_dep, libswscale_dep, libswresample_dep, mbedtls_dep] + winlibs + [chez_scheme_dep],
    install: true,
)
