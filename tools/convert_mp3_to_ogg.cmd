@echo off
rem Every MP3 in a folder and all its sub-folders to an OGG beside it (the game's sound format; tools\audio\mp3_to_ogg.py).
rem   Double-click me: windows ask for the folder and the target frequency, another says how it went.
rem   convert_mp3_to_ogg.cmd <folder>              music quality (160 kbps); a window asks for the frequency
rem   convert_mp3_to_ogg.cmd <folder> --rate 22050 a lower sample rate, a smaller file: 44100 32000 22050 16000 11025 (0: keep)
rem   convert_mp3_to_ogg.cmd <folder> --sfx        sound-effect quality
rem   convert_mp3_to_ogg.cmd <folder> --force    convert again even where the .ogg is up to date
rem   convert_mp3_to_ogg.cmd <folder> --no-gui   for scripts: no windows
rem Needs Python and ffmpeg (tools\get_ffmpeg.cmd downloads it).
python "%~dp0audio\mp3_to_ogg.py" %*
exit /b %ERRORLEVEL%
