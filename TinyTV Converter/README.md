# TinyTV Converter — Batch Convert + Blur Edit

*Modded by EnygmaINC - v1.0.5*

An enhanced fork of [TinyCircuits' official TinyTV Converter](https://tinycircuits.com/products/tinytv-2) adding two quality-of-life features missing from the original: **batch folder conversion** so you can queue an entire folder of videos at once instead of converting one by one, and **Blur Edit** mode which fills the letterbox bars on vertical/portrait videos with a blurred, zoomed copy of the video instead of plain black bars.

> *This was modified from code provided by TinyCircuits. I am in no way affiliated with TinyCircuits or the original project- just a fun mod born out of laziness and the love of a quirky gadget. Use at your own risk.<br><br>For TinyCircuits' official version of the TinyTV Converter - [click here](https://tinytv.us/TinyTV-Converter-App/)*

## What's New (vs. original)

| Feature | Description |
|---------|-------------|
| **Batch Convert** | Select a folder — all supported video files inside are queued and converted using the current interface settings |
| **Blur Edit** | Fills letterbox bars with a blurred, zoomed duplicate of the video. No more black bars on vertical/portrait clips |
| **More Formats** | Added MKV, WMV, FLV, WebM support on top of the original MP4/MOV/AVI/GIF |
| **Keyboard Shortcuts** | `Ctrl+B` for batch convert, `Ctrl+O` open, `Ctrl+Q` quit |

## Features

- **Single Video Conversion**: Convert individual video files with custom settings
- **Batch Conversion**: Convert entire folders of videos at once using current settings
- **Blur Edit**: Fill letterbox bars with a blurred background — ideal for vertical/portrait videos on TinyTV 2
- **Multiple Output Formats**: AVI (recommended) and TSV formats
- **Customizable Settings**: TV type, scaling options, audio normalization
- **Live Preview**: Preview panel updates in real-time as you change settings, including Blur Edit
- **Progress Tracking**: Real-time conversion progress bar
- **Cross-Platform**: Works on Windows, macOS, and Linux

## Supported Input Formats

- MP4, MOV, AVI, GIF, MPG, MPEG
- MKV, WMV, FLV, WebM
- And other formats supported by **FFmpeg**

## Installation

1. Ensure **Python 3.x** is installed ([python.org](https://www.python.org/downloads/))
2. Download **FFmpeg** and place `ffmpeg.exe` (Windows) or `ffmpeg` (macOS/Linux) in the same folder as `TinyTVConverter.py` — [ffmpeg.org/download.html](https://ffmpeg.org/download.html)
3. Run: `python TinyTVConverter.py`

## Usage

### Single Video Conversion

1. **Launch the Application**
   - Run `python TinyTVConverter.py`
   - The application window will open

2. **Select Your Video**
   - Click "Open Video" button or use `Ctrl+O`
   - Choose your video file from the file dialog
   - The video preview and information will be displayed

3. **Configure Settings**
   - **TV Type**: Choose your TinyTV device
     - TinyTV 2 (210x135 resolution)
     - TinyTV Mini (64x64 resolution)
     - TinyTV DIY Kit (96x64 resolution)
   
   - **Scaling Options**:
     - **Contain/Letterbox**: Keep aspect ratio, add padding
     - **Cover/Zoom**: Keep aspect ratio, crop to fit
     - **Fill/Stretch**: Stretch video to fit exactly
     - **Blur Edit**: Fill empty bars with a blurred, zoomed copy of the video (great for vertical/portrait clips on TinyTV 2)
   
   - **Audio Options**:
     - **Keep Audio Volume**: Maintain original volume
     - **Normalize Audio Volume**: Boost quiet audio
   
   - **Output Format**:
     - **AVI** (Default, recommended): Smaller file size
     - **TSV**: For original TinyTV Kit firmware (larger files)

4. **Convert**
   - Click "Convert Video" button
   - Choose output location and filename
   - Monitor progress in the progress bar
   - Conversion will complete automatically

### Batch Conversion

1. **Prepare Your Videos**
   - Place all video files you want to convert in a single folder
   - Supported formats: MP4, MOV, AVI, GIF, MPG, MPEG, MKV, WMV, FLV, WebM

2. **Configure Settings**
   - Set your preferred TV type, scaling, audio, and output format
   - These settings will apply to **all** videos in the batch

3. **Start Batch Conversion**
   - Click **"Batch Convert Folder"** or press `Ctrl+B`
   - Select the folder containing your videos
   - A confirmation dialog shows the file count, output format, TV type, and whether Blur Edit is on
   - Click **Yes** to start

4. **Monitor Progress**
   - Progress bar tracks the current file
   - Console output logs each conversion
   - Converted files are saved alongside the originals in the same folder

### Blur Edit

Blur Edit fills the empty bars on either side of a vertical/portrait video with a blurred, zoomed copy of the same video — the same effect used on social media to make 9:16 clips fill a 16:9 frame.

1. Load a vertical video (e.g. a phone recording shot in portrait)
2. Check **"Blur Edit"** in the Scaling Options panel, or enable it via **Video Options → Blur Edit**
3. The live preview updates immediately so you can see the effect before converting
4. Convert as normal — the blurred background is baked into the output

> Blur Edit always letterboxes the foreground regardless of which Scaling Option radio button is selected. It works with both single and batch conversion.

## Keyboard Shortcuts

- `Ctrl+O`: Open video file
- `Ctrl+B`: Batch convert folder
- `Ctrl+Q`: Quit application

## Menu Options

### File Menu
- **Open...**: Select single video file
- **Convert**: Start single video conversion
- **Batch Convert Folder...**: Convert all videos in a folder
- **Exit**: Close application

### Video Options Menu
- **TV Type**: TinyTV 2, Mini, or DIY Kit
- **Scaling**: Contain/Letterbox, Cover/Zoom, Fill/Stretch
- **Blur Edit** *(checkbox)*: Blurred background fill for vertical videos

### Audio Options Menu
- **Keep Audio Volume**: Maintain original levels
- **Normalize Audio Volume**: Boost quiet audio

### Output Format Menu
- **AVI**: Default format (recommended)
- **TSV**: Original TinyTV Kit format

## Output File Information

- **AVI Files**: Compatible with most TinyTV devices, smaller file size
- **TSV Files**: For original TinyTV DIY Kit firmware, larger file size
- **File Naming**: Output files use the same name as input with appropriate extension
- **Location**: Files are saved in the same folder as the original video

## Tips

1. **For Best Results**:
   - Use "Contain/Letterbox" scaling for most videos
   - Choose AVI format unless you specifically need TSV
   - Enable audio normalization for quiet videos
   - Enable **Blur Edit** for vertical (portrait) videos on TinyTV 2 — it fills the letterbox bars with a blurred, zoomed version of the same video instead of black bars

2. **Batch Processing**:
   - Organize videos in folders by type or project
   - Check settings before starting batch conversion
   - Monitor console output for any errors

3. **File Management**:
   - Original files are never modified
   - Converted files are saved alongside originals
   - Use descriptive folder names for batch processing

## Troubleshooting

**"No video files found"**
- Ensure your folder contains supported video formats
- Check file extensions are lowercase (.mp4, not .MP4)

**"Invalid or unsupported file format"**
- Try a different video file
- Ensure the file isn't corrupted
- Some DRM-protected videos may not work

**Conversion fails**
- Check that FFmpeg is properly included
- Ensure sufficient disk space
- Try converting a single file first

**Audio issues**
- Try enabling "Normalize Audio Volume" for quiet videos
- Some videos may have audio sync issues

## System Requirements

- Python 3.x
- FFmpeg (download separately — not included in source)
- Windows, macOS, or Linux
- Sufficient disk space for converted files

## Version History

| Version | Changes |
|---------|---------|
| **1.0.5** | Added Blur Edit mode (blurred background fill for vertical videos) |
| **1.0.4.1** | Added batch folder conversion, more input formats, keyboard shortcuts |
| 1.0.4 | Original TinyCircuits release |

---

For more information about TinyTV devices: https://tinytv.us
