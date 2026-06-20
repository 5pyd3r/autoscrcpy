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

# App sources
app_src = files(
    'app/application.c',
    'app/window.c',
    'app/options.c',
    'app/cli.c',
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
    app_src,
    dependencies: [libavcodec_dep, libavformat_dep, libavutil_dep, libswscale_dep, libswresample_dep, mbedtls_dep] + winlibs,
    install: true,
)
