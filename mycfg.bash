cd /e/ffmpeg-custom

./configure \
  --logfile=configure.log \
  --prefix=/e/ffmpeg-custom/build \
  --enable-gpl \
  --enable-nonfree \
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
  --enable-libfdk-aac \
  --enable-libopus \
  --disable-debug \
  --disable-doc