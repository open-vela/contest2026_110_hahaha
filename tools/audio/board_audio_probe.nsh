echo "== Qiban board audio probe =="
echo "output dir: /data/audio_test"

mkdir /data/audio_test

echo ""
echo "[1/6] list audio device nodes"
ls /dev/audio
set errcode $?
echo "ls /dev/audio ->" $errcode
ls /dev/audio/*
set errcode $?
echo "ls /dev/audio/* ->" $errcode

echo ""
echo "[2/6] list audio commands in nsh"
help aplay
set errcode $?
echo "help aplay ->" $errcode
help arecord
set errcode $?
echo "help arecord ->" $errcode
help amixer
set errcode $?
echo "help amixer ->" $errcode
help nxplayer
set errcode $?
echo "help nxplayer ->" $errcode
help nxrecorder
set errcode $?
echo "help nxrecorder ->" $errcode

echo ""
echo "[3/6] enumerate playback and capture devices"
aplay -l
set errcode $?
echo "aplay -l ->" $errcode
arecord -l
set errcode $?
echo "arecord -l ->" $errcode

echo ""
echo "[4/6] dump mixer state"
amixer
set errcode $?
echo "amixer ->" $errcode
amixer contents
set errcode $?
echo "amixer contents ->" $errcode

echo ""
echo "[5/6] next manual speaker tone test"
echo "copy these lines into nxplayer:"
echo "device /dev/audio/pcm0p"
echo "tone 16000 2 1000"
echo "q"

echo ""
echo "[6/6] next manual microphone record and playback test"
echo "copy these lines into nxrecorder:"
echo "device /dev/audio/pcm0c"
echo "recordraw /data/audio_test/mic_16k_s16_mono.pcm 1 16 16000 0"
echo "stop"
echo "q"
echo "then copy these lines into nxplayer:"
echo "device /dev/audio/pcm0p"
echo "playraw /data/audio_test/mic_16k_s16_mono.pcm 1 16 16000 0"
echo "q"

echo ""
echo "probe done"
