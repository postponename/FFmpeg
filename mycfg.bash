cd /d/ffmpeg-custom/FFmpeg

./configure \
  --logfile=configure.log \
  --prefix=/d/ffmpeg-custom/FFmpeg/build \
  --enable-gpl \
  --enable-version3 \
  --enable-ffmpeg \
  --enable-ffplay \
  --enable-ffprobe \
  --enable-avfilter \
  --enable-filters \
  --enable-libx264 \
  --enable-libx265 \
  --enable-libdav1d \
  --enable-libfreetype \
  --enable-libfontconfig \
  --enable-libharfbuzz \
  --enable-libmp3lame \
  --enable-libopus \
  --enable-debug=3 \
  --disable-optimizations\
  
