"""mp3_to_ogg.py -- every MP3 in a folder and all its sub-folders to an OGG Vorbis beside it (the game's sound format,
docs/engine/22_AUDIO_GENERATION.md), with ogg.py's ffmpeg.

    python tools/audio/mp3_to_ogg.py <folder>              music quality (160 kbps)
    python tools/audio/mp3_to_ogg.py <folder> --sfx        sound-effect quality (Vorbis q 5)
    python tools/audio/mp3_to_ogg.py <folder> --rate 22050 a lower sample rate: a smaller file (44100, 32000, 22050, 16000, 11025)
    python tools/audio/mp3_to_ogg.py <folder> --force      convert again even where the .ogg is newer than its .mp3
    python tools/audio/mp3_to_ogg.py                       no folder: windows ask for it and for the frequency
    --no-gui                                               no windows (scripts): the result only in the console, exit 1 on a failure

song.mp3 becomes song.ogg in the same folder; the MP3 is kept. An .ogg already newer than its .mp3 is skipped.
A rate only goes down: a file already at or under it keeps its own. With a rate, music is encoded by quality (Vorbis q 4)
instead of 160 kbps, or the lower rate would not make it smaller.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ogg  # noqa: E402

RATES = [  # (Hz, what it is for); 0 = keep the file's own
    (0, 'Keep (as the MP3)'),
    (44100, '44100 Hz - CD quality'),
    (32000, '32000 Hz - a little smaller, most can not hear the difference'),
    (22050, '22050 Hz - about 2/3 the size: fine for sound effects and most music'),
    (16000, '16000 Hz - voices, ambience'),
    (11025, '11025 Hz - smallest, lo-fi'),
]


def size_text(size):
    return f'{size / 1048576:.2f} MB' if size >= 1048576 else f'{size / 1024:.1f} KB'


def message(gui, title, text, error=False):
    print(text)
    if not gui:
        return
    try:
        import tkinter
        from tkinter import messagebox
        root = tkinter.Tk()
        root.withdraw()
        (messagebox.showerror if error else messagebox.showinfo)(title, text)
        root.destroy()
    except Exception:
        pass


def ask_folder():
    try:
        import tkinter
        from tkinter import filedialog
        root = tkinter.Tk()
        root.withdraw()
        folder = filedialog.askdirectory(title='Convert every MP3 in this folder (and its sub-folders) to OGG')
        root.destroy()
        return folder or None
    except Exception:
        return None


def ask_options(folder, rate, sfx, force):
    """The double-click's second window: the target frequency, music or sound effects, convert again. None: cancelled."""
    import tkinter
    root = tkinter.Tk()
    root.title('TOMS: MP3 to OGG')
    root.resizable(False, False)
    chosen = {'ok': False}
    rate_var, sfx_var, force_var = tkinter.IntVar(value=rate), tkinter.BooleanVar(value=sfx), tkinter.BooleanVar(value=force)
    frame = tkinter.Frame(root, padx=16, pady=12)
    frame.pack()
    tkinter.Label(frame, text=f'Folder: {folder}', anchor='w', justify='left').pack(fill='x')
    tkinter.Label(frame, text='Target frequency (lower = smaller file):', anchor='w', font=('Segoe UI', 10, 'bold')).pack(fill='x', pady=(10, 2))
    for hz, text in RATES:
        tkinter.Radiobutton(frame, text=text, variable=rate_var, value=hz, anchor='w').pack(fill='x')
    tkinter.Checkbutton(frame, text='Sound effects (quality 5) instead of music', variable=sfx_var, anchor='w').pack(fill='x', pady=(10, 0))
    tkinter.Checkbutton(frame, text='Convert again even where the OGG is up to date', variable=force_var, anchor='w').pack(fill='x')
    buttons = tkinter.Frame(frame, pady=10)
    buttons.pack()

    def ok():
        chosen['ok'] = True
        root.destroy()
    tkinter.Button(buttons, text='Convert', width=12, command=ok, default='active').pack(side='left', padx=4)
    tkinter.Button(buttons, text='Cancel', width=12, command=root.destroy).pack(side='left', padx=4)
    root.bind('<Return>', lambda _e: ok())
    root.bind('<Escape>', lambda _e: root.destroy())
    root.attributes('-topmost', True)
    root.mainloop()
    return (rate_var.get(), sfx_var.get(), force_var.get()) if chosen['ok'] else None


def main():
    parser = argparse.ArgumentParser(description='Every MP3 under a folder to an OGG beside it.')
    parser.add_argument('folder', nargs='?', help='the folder to scan (all sub-folders too)')
    parser.add_argument('--sfx', action='store_true', help='sound-effect quality (Vorbis q 5) instead of music (160 kbps)')
    parser.add_argument('--rate', type=int, choices=[hz for hz, _ in RATES], default=None,
                        help='the target sample rate in Hz, lower = smaller (0 or none: keep the file\'s own)')
    parser.add_argument('--force', action='store_true', help='convert again even where the .ogg is up to date')
    parser.add_argument('--no-gui', action='store_true', help='no windows: console only')
    args = parser.parse_args()
    gui = not args.no_gui
    title = 'TOMS: MP3 to OGG'

    asked = args.folder is None   # a double-click: ask for everything
    folder = args.folder or (ask_folder() if gui else None)
    if not folder:
        message(gui, title, 'No folder chosen: nothing converted.')
        return 0
    root = Path(folder).resolve()
    if not root.is_dir():
        message(gui, title, f'Not a folder: {root}', error=True)
        return 1
    rate, sfx, force = args.rate or 0, args.sfx, args.force
    if gui and (asked or args.rate is None):
        try:
            options = ask_options(root, rate, sfx, force)
        except Exception:
            options = (rate, sfx, force)   # (no window possible: the defaults)
        if options is None:
            message(gui, title, 'Cancelled: nothing converted.')
            return 0
        rate, sfx, force = options
    try:
        ffmpeg = ogg.find_ffmpeg()
    except SystemExit as stop:   # (find_ffmpeg stops with how to get it)
        message(gui, title, str(stop), error=True)
        return 1

    quality = ogg.SFX_QUALITY if sfx else (ogg.MUSIC_QUALITY if rate else ogg.MUSIC_BITRATE)
    sources = sorted(p for p in root.rglob('*') if p.is_file() and p.suffix.lower() == '.mp3')
    done, skipped, failed, bigger = 0, 0, [], []
    size_in, size_out = 0, 0
    print(f'{len(sources)} MP3 file(s) under {root}; {"keep the frequency" if not rate else f"{rate} Hz"}; {"sound effects" if sfx else "music"}')
    for index, source in enumerate(sources, 1):
        target = source.with_suffix('.ogg')
        name = source.relative_to(root)
        if not force and target.exists() and target.stat().st_mtime >= source.stat().st_mtime:
            skipped += 1
            print(f'[{index}/{len(sources)}] up to date: {name}')
            continue
        own = ogg.sample_rate(source, ffmpeg) if rate else None
        use_rate = rate if rate and (own is None or own > rate) else None   # only ever down
        print(f'[{index}/{len(sources)}] {name}' + (f'  ({own} -> {use_rate} Hz)' if use_rate else ''))
        why = ogg.convert(source, target, quality, ffmpeg, use_rate)
        if why and quality is ogg.MUSIC_BITRATE:   # a low-rate MP3 cannot take 160 kbps in Vorbis: by quality then
            why = ogg.convert(source, target, ogg.MUSIC_QUALITY, ffmpeg, use_rate)
        if why:
            failed.append(f'{name}: {why.splitlines()[-1]}')   # (ffmpeg's last line says it)
            target.unlink(missing_ok=True)   # (no half-written file left behind)
        else:
            done += 1
            before, after = source.stat().st_size, target.stat().st_size
            size_in += before
            size_out += after
            if after >= before:   # no smaller than the MP3: say which, and by how much
                bigger.append(f'{name}: MP3 {size_text(before)} -> OGG {size_text(after)} (+{(after - before) * 100.0 / max(1, before):.0f} %)')
                print(f'    bigger than the MP3: {size_text(before)} -> {size_text(after)}')

    summary = f'{root}\n\nConverted: {done}\nAlready up to date: {skipped}\nFailed: {len(failed)}'
    if done:
        summary += f'\n\nMP3 {size_in / 1048576:.2f} MB -> OGG {size_out / 1048576:.2f} MB'
    if bigger:
        summary += (f'\n\nNot smaller than the MP3 ({len(bigger)}): try a lower frequency or sound-effect quality\n'
                    + '\n'.join(bigger[:15]) + ('\n...' if len(bigger) > 15 else ''))
    if not sources:
        summary = f'No MP3 files under {root}.'
    if failed:
        summary += '\n\n' + '\n'.join(failed[:10]) + ('\n...' if len(failed) > 10 else '')
    message(gui, title, summary, error=bool(failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
