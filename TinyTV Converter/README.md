# TinyTV Converter — Batch Convert Beta

*Modded by Roman "EnygmaINC" Corrales — July 30, 2025*

A Python application for converting video files to formats compatible with TinyTV devices (TinyTV 2, TinyTV Mini, and TinyTV DIY Kit).

## Features

- **Single Video Conversion**: Convert individual video files with custom settings
- **Batch Conversion**: Convert entire folders of videos at once
- **Multiple Output Formats**: AVI (recommended) and TSV formats
- **Customizable Settings**: TV type, scaling options, audio settings
- **Progress Tracking**: Real-time conversion progress
- **Cross-Platform**: Works on Windows, macOS, and Linux

## Supported Input Formats

- MP4, MOV, AVI, GIF, MPG, MPEG
- MKV, WMV, FLV, WebM
- And other formats supported by FFmpeg

## Installation

1. Ensure you have Python 3.x installed
2. Make sure FFmpeg is available (included in the package)
3. Run the application: `python TinyTVConverter.py`

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
   - These settings will apply to all videos in the batch

3. **Start Batch Conversion**
   - Click "Batch Convert Folder" button or use `Ctrl+B`
   - Select the folder containing your videos
   - Review the confirmation dialog showing:
     - Number of videos found
     - Current output format
     - Current TV type settings
   - Click "Yes" to start conversion

4. **Monitor Progress**
   - Progress bar shows current file conversion
   - Console output shows conversion status
   - All converted files will be saved in the same folder as originals

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
- FFmpeg (included)
- Windows, macOS, or Linux
- Sufficient disk space for converted files

## Version

TinyTV Converter - Python 1.0.4

---

For more information about TinyTV devices, visit the official TinyTV website. 
https://tinytv.us