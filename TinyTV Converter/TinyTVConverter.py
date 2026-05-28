# http://stackoverflow.com/questions/29158220/tkinter-understanding-mainloop
# http://stackoverflow.com/questions/24330178/scrolling-progress-bar-in-tkinter
# http://www.programcreek.com/python/example/5376/subprocess.STARTUPINFO
# https://superuser.com/questions/547296/resizing-videos-with-ffmpeg-avconv-to-fit-into-static-sized-player

#drag and drop files- apparently this needs a Tk extension
#unicode filenames- hopefully this works with python3, tbd
#better error messages?
#check with random file types
#check conversion command params
#adjust bit rate calculation
#add version number

import os
import sys
import argparse
import subprocess as sp
import re
import time

from tkinter import Tk, IntVar, DoubleVar, BOTH, Text, Menu, END, X, W, E, NW, PhotoImage, Image, Canvas, Toplevel, Grid, messagebox
from tkinter import Label as tkLabel
import tkinter.filedialog
from tkinter.ttk import Progressbar, Style, Button, Radiobutton, Checkbutton, Frame, Label, LabelFrame, Entry
import threading

class CreateToolTip(object):
    """
    create a tooltip for a given widget
    """
    def __init__(self, widget, text='widget info'):
        self.waittime = 800     #miliseconds
        self.wraplength = 300   #pixels
        self.widget = widget
        self.text = text
        self.widget.bind("<Enter>", self.enter)
        self.widget.bind("<Leave>", self.leave)
        self.widget.bind("<ButtonPress>", self.leave)
        self.id = None
        self.tw = None

    def enter(self, event=None):
        self.schedule()

    def leave(self, event=None):
        self.unschedule()
        self.hidetip()

    def schedule(self):
        self.unschedule()
        self.id = self.widget.after(self.waittime, self.showtip)

    def unschedule(self):
        id = self.id
        self.id = None
        if id:
            self.widget.after_cancel(id)

    def showtip(self, event=None):
        #x = y = 0
        #x, y, cx, cy = self.widget.bbox("insert")
        #x += self.widget.winfo_rootx() + self.widget.winfo_width()+ 2
        #y += self.widget.winfo_rooty() + 2
        x=self.widget.winfo_pointerx();
        y=self.widget.winfo_pointery()+20;
        # creates a toplevel window
        self.tw = Toplevel(self.widget)
        #if sys.platform=='darwin' :
        #    self.tw = Toplevel(self.widget,background='#ECECEC')
        # Leaves only the label and removes the app window
        self.tw.wm_overrideredirect(True)
        self.tw.wm_geometry("+%d+%d" % (x, y))
        if sys.platform=='darwin' :
            label = Label(self.tw, text=self.text, justify='left', font=('Arial', 11),
                       background="#ffffff", relief='solid', borderwidth=0,
                       wraplength = self.wraplength)
            label.pack(padx=5, pady=5)
        else:
            label = tkLabel(self.tw, text=' '+self.text+' ', justify='left',
                       background="#FFFFE1", relief='solid', borderwidth=1,
                       wraplength = self.wraplength)
            label.pack(padx=0, pady=0)

    def hidetip(self):
        tw = self.tw
        self.tw= None
        if tw:
            tw.destroy()

def resource_path(relative_path):
    """ Get absolute path to resource, works for dev and for PyInstaller """
    try:
        # PyInstaller creates a temp folder and stores path in _MEIPASS
        base_path = sys._MEIPASS
    except Exception:
        base_path = os.path.abspath(".")

    return os.path.join(base_path, relative_path)

print((sys.platform))
FFMPEG_BIN='ffmpeg'

if os.name=='nt' :
    FFMPEG_BIN=resource_path('ffmpeg.exe')
if sys.platform=='darwin' :
    FFMPEG_BIN=resource_path('ffmpeg')

class TinyTVConverter(Frame):
  
    def __init__(self, parent):
        
        Frame.__init__(self, parent )   #, background="white"
        self.parent = parent
        self.initVideoData()
        self.TVTypeOption = IntVar()
        self.videoWindowOption = IntVar()
        self.audioEnable = IntVar()
        self.progressBarVar = IntVar()
        self.videoBitDepth = IntVar()
        self.outputFormat = IntVar()
        self.normalizeAudio = IntVar()
        self.blurEdit = IntVar()
        self.initUI()
        
    def initVideoData(self):
        #currently constants
        self.tsvAudioSampleBitDepth=10
        self.tsvAudioSampleCountPerFrame=1024
        
        #no longer constants, reset in calculateVideoData
        self.outputWidth=210
        self.outputHeight=135
        self.outputFrameRate=30.0
        self.outputAudioSampleRate=10000
        self.outputBytesPerSecond=30000
        
        #variables that are displayed
        self.inputFile = '-'
        self.outputFile = '-'
        self.durationString = "--:--:--"
        self.outputSize = 0
        
        #variables that change when video or options selected
        self.durationSeconds = 0.0
        self.inputVidFrameBytes = 0
        self.outputVidFrameBytes = 0
        self.audioFrameBytes = 0
        self.totalFrames = 0.0
        self.volumeAdjust = 0.0

    def calculateVideoData(self):
        if (self.TVTypeOption.get() == 3):
            self.outputWidth=210
            self.outputHeight=135
            if(self.outputFormat.get() == 1):
                self.outputFrameRate=24.0
                self.outputAudioSampleRate=10000
                self.outputBytesPerSecond=34000
            else:
                self.outputFrameRate=30.0
                self.outputAudioSampleRate=self.outputFrameRate*self.tsvAudioSampleCountPerFrame
                self.outputBytesPerSecond= self.outputFrameRate*self.outputWidth*self.outputHeight*2 + self.outputAudioSampleRate*2
        if (self.TVTypeOption.get() == 2):
            self.outputWidth=64
            self.outputHeight=64
            if(self.outputFormat.get() == 1):
                self.outputFrameRate=24.0
                self.outputAudioSampleRate=10000
                self.outputBytesPerSecond=24000
            else:
                self.outputFrameRate=30.0
                self.outputAudioSampleRate=self.outputFrameRate*self.tsvAudioSampleCountPerFrame
                self.outputBytesPerSecond= self.outputFrameRate*self.outputWidth*self.outputHeight*2 + self.outputAudioSampleRate*2
        if (self.TVTypeOption.get() == 1):
            self.outputWidth=96
            self.outputHeight=64
            if(self.outputFormat.get() == 1):
                self.outputFrameRate=24.0
                self.outputAudioSampleRate=10000
                self.outputBytesPerSecond=25000
            else:
                self.outputFrameRate=30.0
                self.outputAudioSampleRate=self.outputFrameRate*self.tsvAudioSampleCountPerFrame
                self.outputBytesPerSecond= self.outputFrameRate*self.outputWidth*self.outputHeight*2 + self.outputAudioSampleRate*2
        
        self.inputVidFrameBytes = self.outputWidth*self.outputHeight*2
        if (self.videoBitDepth.get() == 8):
            self.outputVidFrameBytes=self.inputVidFrameBytes/2
        else:
            self.outputVidFrameBytes=self.inputVidFrameBytes
        
        self.audioFrameBytes=self.tsvAudioSampleCountPerFrame*2
        
        self.totalFrames=self.durationSeconds*self.outputFrameRate
        #self.ouputSize=self.totalFrames*(self.outputVidFrameBytes+self.audioFrameBytes)
        self.outputSize=self.durationSeconds*self.outputBytesPerSecond

    def initUI(self):
      
        self.parent.title("TinyTV Converter")
        if os.name=='nt':
            self.parent.title("TinyTV Converter - 1.0.4")
        self.pack(fill=BOTH, expand=1)
        
        menubar = Menu(self.parent)
        self.parent.config(menu=menubar)
        
        menubar = Menu(self.parent)
        self.parent.config(menu=menubar)
        
        fileMenu = Menu(menubar, tearoff=0)

        menubar.add_cascade(label="File", menu=fileMenu)
        fileMenu.add_command(label="Open...", command=self.onOpen, accelerator="Ctrl+O")
        fileMenu.add_command(label="Convert", command=self.onConvert)
        fileMenu.add_command(label="Batch Convert Folder...", command=self.onBatchConvert, accelerator="Ctrl+B")
        fileMenu.add_separator()
        fileMenu.add_command(label="Exit", command=self.onQuit, accelerator="Ctrl+Q")

        optionsMenu = Menu(menubar, tearoff=0)
        menubar.add_cascade(label="Video Options", menu=optionsMenu)
        optionsMenu.add_radiobutton(label="TinyTV 2", command=self.displayVidData, var=self.TVTypeOption, value=3)
        optionsMenu.add_radiobutton(label="TinyTV Mini",  command=self.displayVidData, var=self.TVTypeOption, value=2)
        optionsMenu.add_radiobutton(label="TinyTV DIY Kit",  command=self.displayVidData, var=self.TVTypeOption, value=1)
        optionsMenu.add_separator()
        optionsMenu.add_radiobutton(label="Contain/Letterbox", command=self.displayVidData, var=self.videoWindowOption, value=3)
        optionsMenu.add_radiobutton(label="Cover/Zoom", command=self.displayVidData, var=self.videoWindowOption, value=2)
        optionsMenu.add_radiobutton(label="Fill/Stretch", command=self.displayVidData, var=self.videoWindowOption, value=1)
        optionsMenu.add_separator()
        optionsMenu.add_checkbutton(label="Blur Edit (fill background)", variable=self.blurEdit, command=self.displayVidData)
        optionsMenu = Menu(menubar, tearoff=0)
        menubar.add_cascade(label="Audio Options", menu=optionsMenu)
        optionsMenu.add_radiobutton(label="Keep Audio Volume", var=self.normalizeAudio, value=0)
        optionsMenu.add_radiobutton(label="Normalize (Increase) Audio Volume", var=self.normalizeAudio, value=1)
        optionsMenu = Menu(menubar, tearoff=0)
        menubar.add_cascade(label="Output Format", menu=optionsMenu)
        optionsMenu.add_radiobutton(label=".AVI (Default, recommended)", command=self.displayVidData, var=self.outputFormat, value=1)
        optionsMenu.add_radiobutton(label=".TSV (For original TinyTV Kit firmware, enormous file size!)",  command=self.displayVidData, var=self.outputFormat, value=2)
        
        
        
        
        self.TVTypeOption.set(3)
        self.videoWindowOption.set(3)
        self.audioEnable.set(1)
        self.videoBitDepth.set(16)
        self.outputFormat.set(1)
        self.normalizeAudio.set(0)
        self.blurEdit.set(0)
        
        self.bind_all("<Control-o>", self.onOpen)
        self.bind_all("<Control-b>", self.onBatchConvert)
        #self.bind_all("<Control-r>", self.onConvert)
        self.bind_all("<Control-q>", self.onQuit)

        
        Grid.columnconfigure(self, 0, weight=0)
        Grid.columnconfigure(self, 1, weight=0,minsize=75)
        Grid.columnconfigure(self, 2, weight=0,minsize=75)
        #Grid.columnconfigure(self, 3, weight=0,minsize=100)
        
        canvasBG = 'white'
        if sys.platform=='darwin' :
            canvasBG = 737373
        
        self.preview = Canvas(self, width = self.outputWidth*2, height = self.outputHeight*2, bg = canvasBG)
        self.preview.grid(column=0,row=0, rowspan=6, padx=(5,0), pady=5)
        
        
        self.image = PhotoImage(file=resource_path('splash.png'))
        
        previewX=0
        previewY=0
        if os.name=='darwin':
            previewX=3
            previewY=2
        self.preview.create_image(previewX,previewY,image=self.image,anchor=NW)

        
        

        self.openFileFrame = Frame(self)

        self.openFileButton = Button(self.openFileFrame, text='Open Video', command=self.onOpen)
        self.openFileButton.pack(fill='x')


        self.radioFrameTVType = LabelFrame(self, text = 'TV Type')
                
        self.radButtonTV2 = Radiobutton(self.radioFrameTVType, text="TinyTV 2",  command=self.displayVidData, var=self.TVTypeOption, value=3)
        self.radButtonTV2.pack(fill='x')

        self.radButtonTVMini = Radiobutton(self.radioFrameTVType, text="TinyTV Mini",  command=self.displayVidData, var=self.TVTypeOption, value=2)
        self.radButtonTVMini.pack(fill='x')

        self.radButtonTVKit = Radiobutton(self.radioFrameTVType, text="TinyTV DIY Kit", command=self.displayVidData, var=self.TVTypeOption, value=1)
        self.radButtonTVKit.pack(fill='x')


        
        self.radioFrameVid = LabelFrame(self, text = 'Scaling Options')
        
        self.radButton3 = Radiobutton(self.radioFrameVid, text="Contain/Letterbox",  command=self.displayVidData, var=self.videoWindowOption, value=3)
        self.radButton3.pack(fill='x')

        self.radButton2 = Radiobutton(self.radioFrameVid, text="Cover/Zoom",  command=self.displayVidData, var=self.videoWindowOption, value=2)
        self.radButton2.pack(fill='x')

        self.radButton1 = Radiobutton(self.radioFrameVid, text="Fill/Stretch", command=self.displayVidData, var=self.videoWindowOption, value=1)
        self.radButton1.pack(fill='x')

        self.blurEditCheckbox = Checkbutton(self.radioFrameVid, text='Blur Edit', variable=self.blurEdit, command=self.displayVidData)
        self.blurEditCheckbox.pack(fill='x', pady=(3,0))


        self.audioFrame = Frame(self)

        self.audioCheckBox = Checkbutton(self.audioFrame, text='Boost Audio Volume', command=self.runVolumeDetect)
        self.audioCheckBox.pack(fill='x')
                
        self.convertFileFrame = Frame(self)

        self.convertFileButton = Button(self.convertFileFrame, text='Convert Video', command=self.onConvert)
        self.convertFileButton.pack(fill='x')
        
        self.batchConvertButton = Button(self.convertFileFrame, text='Batch Convert Folder', command=self.onBatchConvert)
        self.batchConvertButton.pack(fill='x', pady=(5,0))
        
        
        
        
        #self.openFolderButton = Button(self.radioFrame, text='Open Folder', command=self.openFolder)
        #self.openFolderButton.pack(fill='x')
        
        #self.quitButton = Button(self.radioFrame, text='Quit', command=self.onQuit)


        self.openFileFrame.grid(column=1,row=0,columnspan=2, sticky=W+E, padx=5, pady=(5,0))
        self.radioFrameTVType.grid(column=1,row=1,columnspan=2,sticky=W+E, padx=5, pady=1)
        self.radioFrameVid.grid(column=1,row=2,columnspan=2, sticky=W+E, padx=5, pady=1)
        
        self.durationStringLabel = Label(self, text="Video Length:")
        self.durationStringLabel.grid(column=1,row=3, sticky=W, padx=5, pady=1)
        self.durationStringField = Label(self)
        self.durationStringField.grid(column=2,row=3, sticky=E, padx=5, pady=1)
        
        
        self.outputSizeLabel = Label(self, text="Output Size:")
        self.outputSizeLabel.grid(column=1,row=4, sticky=W, padx=5, pady=1)

        self.outputSizeField = Label(self)
        self.outputSizeField.grid(column=2,row=4, sticky=E, padx=5, pady=1)
        
        self.convertFileFrame.grid(column=1,row=5,columnspan=2, sticky=W+E, padx=5, pady=5)
        
        
        
        
        
        
        
        progressbar = Progressbar(self, variable=self.progressBarVar, maximum=100)
        progressbar.grid(column=0,row=6,columnspan=3, sticky=W+E, padx=5, pady=(0,5))
        
        openFileButtonTip = CreateToolTip(self.openFileButton, 'Open a video file or GIF')
        radButtonTinyTV2Tip = CreateToolTip(self.radButtonTV2, 'Convert video for TinyTV 2')
        radButtonTinyTVMiniTip = CreateToolTip(self.radButtonTVMini, 'Convert video for TinyTV Mini')
        radButtonTinyTVKitTip = CreateToolTip(self.radButtonTVKit, 'Convert video for TinyTV DIY Kit')
        radButton3Tip = CreateToolTip(self.radButton3, 'Keep aspect ratio and add padding to fit TinyTV')
        radButton2Tip = CreateToolTip(self.radButton2, 'Keep aspect ratio and crop to fit TinyTV')
        radButton1Tip = CreateToolTip(self.radButton1, 'Stretch video to fit TinyTV')
        blurEditTip = CreateToolTip(self.blurEditCheckbox, 'Fill empty side bars with a blurred, zoomed version of the video. Best for vertical/portrait clips on TinyTV 2. Overrides scaling mode for the foreground (always letterboxed).')
        convertFileButtonTip = CreateToolTip(self.convertFileButton, 'Start video conversion')
        batchConvertButtonTip = CreateToolTip(self.batchConvertButton, 'Convert all video files in a folder using current settings')
        #openFolderButtonTip = CreateToolTip(self.openFolderButton, 'Open folder containing converted video file')

        self.displayVidData()
          
    def getScaleCommand(self,width,height):
        scaleCommand='scale=%d:%d,hqdn3d' % (width, height)
        if (self.videoWindowOption.get() == 2):
            scaleCommand='scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d:exact=1,hqdn3d' % (width, height, width, height) #,setsar=1 ?
        if (self.videoWindowOption.get() == 3):
            scaleCommand='scale=%d:%d:force_original_aspect_ratio=decrease,format=yuv444p,pad=%d:%d:(ow-iw)/2:(oh-ih)/2,format=yuv420p,hqdn3d' % (width,height,width,height)
        #https://forum.videohelp.com/threads/401057-padding-top-and-bottom-odd-number-ffmpeg
        return scaleCommand

    def getBlurFilterComplex(self, width, height, output_label='vout'):
        """Returns a -filter_complex string for the Blur Edit effect.

        Duplicates the stream: one copy is scaled to cover + heavily blurred for the
        background; the other is letterboxed and overlaid on top. The result fills
        horizontal space (e.g. TinyTV 2's 210x135 canvas) when the source is a
        vertical/portrait video with no black bars.
        """
        # Background layer: zoom to fill the full canvas, then blur hard
        bg = ('scale=%d:%d:force_original_aspect_ratio=increase,'
              'crop=%d:%d,'
              'boxblur=luma_radius=10:luma_power=2') % (width, height, width, height)

        # Foreground layer: scale to fit canvas — NO padding.
        # The overlay filter centers it over the blurred background so the
        # bg shows through on the sides instead of opaque black bars.
        fg = ('scale=%d:%d:force_original_aspect_ratio=decrease,'
              'format=yuv420p') % (width, height)

        return ('[0:v]split=2[_be_bg][_be_fg];'
                '[_be_bg]%s[_be_blurred];'
                '[_be_fg]%s[_be_front];'
                '[_be_blurred][_be_front]overlay=(W-w)/2:(H-h)/2,hqdn3d[%s]') % (bg, fg, output_label)

    def displayPreviewFrame(self):
        if(self.durationSeconds>0):
            previewTime=self.durationSeconds/2
            m, s = divmod(previewTime, 60)
            h, m = divmod(m, 60)

            previewTime = '%02d:%02d:%02d' % (h, m, s)

            if self.blurEdit.get() == 1:
                filterGraph = self.getBlurFilterComplex(self.outputWidth*2, self.outputHeight*2)
                vidcommand = [ FFMPEG_BIN,
                '-ss', previewTime,
                '-i', self.inputFile,
                '-f', 'image2pipe',
                '-filter_complex', filterGraph,
                '-map', '[vout]',
                '-pix_fmt', 'rgb24',
                '-vcodec', 'rawvideo', '-']
            else:
                scaleCommand = self.getScaleCommand(self.outputWidth*2, self.outputHeight*2)
                vidcommand = [ FFMPEG_BIN,
                '-ss', previewTime,
                '-i', self.inputFile,
                '-f', 'image2pipe',
                '-vf', scaleCommand,
                '-pix_fmt', 'rgb24',
                '-vcodec', 'rawvideo', '-']
            infoPipe = '';
            if os.name=='nt' :
                startupinfo = sp.STARTUPINFO()
                startupinfo.dwFlags |= sp.STARTF_USESHOWWINDOW
                infoPipe=sp.Popen(vidcommand, stdin = sp.PIPE, stdout = sp.PIPE, stderr = sp.DEVNULL, bufsize=(self.outputWidth*2 * self.outputHeight*2)*3, startupinfo=startupinfo)
            else:
                infoPipe=sp.Popen(vidcommand, stdin = sp.PIPE, stdout = sp.PIPE, stderr = sp.DEVNULL, bufsize=(self.outputWidth*2 * self.outputHeight*2)*3)
                
            vidFrame = infoPipe.stdout.read((self.outputWidth*2 * self.outputHeight*2)*3)
            
            infoPipe.terminate()
            infoPipe.stdout.close()
            infoPipe.wait()
            
            xdataPrependStr = 'P6 %s %s 255 ' % (self.outputWidth*2, self.outputHeight*2)
            xdata = bytes(xdataPrependStr, encoding="raw_unicode_escape") + bytearray(vidFrame)
            
            self.image = PhotoImage(width=self.outputWidth*2, height=self.outputHeight*2, data=xdata , format='PPM')
            
            xOffset = self.preview.winfo_width() - self.image.width()
            if(xOffset):
                xOffset=xOffset/2
            yOffset = self.preview.winfo_height() - self.image.height()
            if(yOffset):
                yOffset=yOffset/2
            
            self.preview.create_image(xOffset,yOffset,image=self.image,anchor=NW)

    def displayVidData(self):
        self.calculateVideoData()
        
        
        #self.preview.config(width=self.outputWidth*2, height=self.outputHeight*2)
        
        size=self.outputSize
        sizeUnit='B';
        if (size>1024.0):
            size/=1024.0
            sizeUnit='KB'
            if (size>1024.0):
                size/=1024.0
                sizeUnit='MB'
                if (size>1024.0):
                    size/=1024.0
                    sizeUnit='GB'

        #self.inputNameField.config(text=os.path.split(self.inputFile)[1])
        #self.outputNameField.config(text=os.path.split(self.outputFile)[1])
        self.durationStringField.config(text=self.durationString)
       
        if size > 0:
            self.outputSizeField.config(text= "~%.0f %s" % (size, sizeUnit))
        else:
            self.outputSizeField.config(text = "-")
        #self.conversionTimeField.config(text = "--:--:--")
        self.progressBarVar.set(0.0)
        self.displayPreviewFrame()
        
    def onOpen(self):
      
        ftypes = [('Video files', '*.mp4;*.mov;*.mpg;*.mpeg;*.avi;*.gif'), ('All files', '*')]
        #dlg = tkinter.filedialog.Open(self, filetypes = ftypes) # makes macOS angry
        dlg = tkinter.filedialog.Open(self)
        fileName = dlg.show()
        
        if fileName != '':
            self.inputFile = fileName
            self.outputFile = "%s.avi" % (os.path.splitext(self.inputFile)[0])
            #print(self.inputFile)
            #print(self.outputFile)
            infoPipe = '';
            if os.name=='nt' :
                startupinfo = sp.STARTUPINFO()
                startupinfo.dwFlags |= sp.STARTF_USESHOWWINDOW
                infoPipe=sp.Popen([FFMPEG_BIN,"-i",self.inputFile], stdin = sp.PIPE, stdout = sp.DEVNULL, stderr = sp.PIPE, bufsize=1000000, startupinfo=startupinfo)
            else:
                infoPipe=sp.Popen([FFMPEG_BIN,"-i",self.inputFile], stdin = sp.PIPE, stdout = sp.DEVNULL, stderr = sp.PIPE, bufsize=1000000)
            #infoPipe.stdout.read()  #removed for videos with chapter markers embedded
            info = infoPipe.stderr.read().decode('utf8')
            infoPipe.terminate()
            #infoPipe.stdout.close()
            infoPipe.stderr.close()  #added for videos with chapter markers embedded
            infoPipe.wait()
            #print(info)
            if 'Invalid' in info:
                self.initVideoData()
                self.displayVidData()
                self.inputNameField.config(text='Unsupported file!')
                print('FFmpeg unrecognized file')
                return
            
            lines = info.splitlines()
            
            try:
                keyword = 'Duration: '
                line = [l for l in lines if keyword in l][0]
                match = re.findall("([0-9][0-9]:[0-9][0-9]:[0-9][0-9].[0-9][0-9])", line)[0]
                self.durationString = match[0:8]
                self.durationSeconds = float(match[0:2])*60.0*60.0 + float(match[3:5])*60.0 + float(match[6:11])
            except:
                if 'N/A' in info:
                    #gif or image- need to 'decode' video to determine duration.
                    vidcommand = [ FFMPEG_BIN,
                    '-i', self.inputFile,
                    '-f', 'null', '-']
                    infoPipe = '';
                    if os.name=='nt' :
                        startupinfo = sp.STARTUPINFO()
                        startupinfo.dwFlags |= sp.STARTF_USESHOWWINDOW
                        infoPipe=sp.Popen(vidcommand, stdin = sp.PIPE, stdout = sp.PIPE, stderr = sp.PIPE, bufsize=(self.outputWidth*2 * self.outputHeight*2)*3, startupinfo=startupinfo)
                    else:
                        infoPipe=sp.Popen(vidcommand, stdin = sp.PIPE, stdout = sp.PIPE, stderr = sp.PIPE, bufsize=(self.outputWidth*2 * self.outputHeight*2)*3)
                    
                    info = infoPipe.stdout.readline()
                    infoerr = infoPipe.stderr.read().decode('utf8')
                    infoPipe.terminate()
                    infoPipe.stdout.close()
                    infoPipe.wait()
                    lines = infoerr.splitlines()
                    #print(infoerr)
                    try:
                        keyword = 'time='
                        line = [l for l in lines if keyword in l][0]
                        finalTimeString=  line.split('time=')[1].split(' ')[0]
                        finalTimeStringSplit=  finalTimeString.split(':')
                        if len(finalTimeStringSplit) == 1:
                            self.durationSeconds = float(finalTimeStringSplit[0])
                        if len(finalTimeStringSplit) == 2:
                            self.durationSeconds = float(finalTimeStringSplit[0])*60.0
                            self.durationSeconds += float(finalTimeStringSplit[1])
                        if len(finalTimeStringSplit) == 3:
                            self.durationSeconds = float(finalTimeStringSplit[0])*60.0*60.0
                            self.durationSeconds = float(finalTimeStringSplit[1])*60.0
                            self.durationSeconds += float(finalTimeStringSplit[2])
                        hoursToDisplay=int(self.durationSeconds)/3600
                        minutesToDisplay=int(self.durationSeconds)/60
                        secondsToDisplay= int(self.durationSeconds)%60
                        if(hoursToDisplay==0 and minutesToDisplay==0 and secondsToDisplay==0):
                            secondsToDisplay=1
                        self.durationString = "%02d:%02d:%02d" % (hoursToDisplay, minutesToDisplay,secondsToDisplay)
                    except:
                        print ("Error finding video duration")
                        self.inputNameField.config(text='Error decoding file')
                        self.durationSeconds = 1.0/29.99
                        self.durationString = "--:--:--"
                else:
                    self.initVideoData()
                    self.displayVidData()
                    print ("Video info error")
                    self.inputNameField.config(text="Video info error")
                    return
                
            self.displayVidData()

    def onQuit(self, event=''):
        self.quit()
        #self.destroy()
    
    def openFolder(self, event=''):
        if os.name=='nt' :
            infoPipe=sp.Popen(['explorer', os.path.normpath(os.path.split(self.inputFile)[0])])
        else:
            infoPipe=sp.Popen(['open', os.path.normpath(os.path.split(self.inputFile)[0])])

    def onConvert(self, event=''):
        if(self.outputSize==0):
            messagebox.showwarning('TinyTV Converter', 'No video selected!')
            return
        extStr = ".avi"
        fileTypeDescrip = 'AVI compatible with TinyTV'
        if self.outputFormat.get() == 2:
            extStr = ".tsv"
            fileTypeDescrip = 'TSV compatible with TinyTV DIY Kit'
        saveNewVideoAs = tkinter.filedialog.asksaveasfilename(initialfile=(os.path.splitext(os.path.split(self.inputFile)[1])[0]),defaultextension=extStr,filetypes=[(fileTypeDescrip, '*'+extStr)] ) #-confirmoverwrite, -defaultextension, -filetypes, -initialdir, -initialfile, -parent, -title, or -typevariable
        if(len(saveNewVideoAs) < 1):
            return
        if(os.path.splitext(saveNewVideoAs)[1] != extStr):
            messagebox.showwarning('TinyTV Converter', 'File extension should be ' + extStr + ', not ' + os.path.splitext(saveNewVideoAs)[1] + ', please retry!')
        #print(self.outputFile)
        #print(saveNewVideoAs)
        self.outputFile = saveNewVideoAs
        
        self.volumeAdjust = 0.0
        if self.normalizeAudio.get() == 1 :
            self.volumeAdjust = (0-self.runVolumeDetect(True)) * 0.95
            messagebox.showwarning('TinyTV Converter', 'Adjusting volume by ' + "%.1f dB" % (self.volumeAdjust))
        
        
        convertThread = threading.Thread(target=self.convertAVI, args=())
        if(self.outputFormat.get() == 2):
            convertThread = threading.Thread(target=self.convertTSV, args=())
        convertThread.daemon=True
        convertThread.start()

    def runVolumeDetect(self,afterResample):
        if(len(self.inputFile)<5):
            return 0.0
        filter = ''
        if afterResample :
            filter = 'aresample=10000,aresample=async=1000,asetnsamples=n=210:p=0,aresample=osf=u8,'
        volumeDetectCommand = [ FFMPEG_BIN,
            '-i', self.inputFile,
            '-vn',
            '-ac', '1',
            '-af', filter+'volumedetect',
            '-f', 'null', '/dev/null'
            ]
        
        #print(volumeDetectCommand)
        cmdPipe = '';
        if os.name=='nt' :
            startupinfo = sp.STARTUPINFO()
            startupinfo.dwFlags |= sp.STARTF_USESHOWWINDOW
            cmdPipe=sp.Popen(volumeDetectCommand, stdout = sp.PIPE, stderr=sp.STDOUT, universal_newlines=True, startupinfo=startupinfo)
        else:
            cmdPipe=sp.Popen(volumeDetectCommand, stdout = sp.PIPE, stderr=sp.STDOUT, universal_newlines=True)
        maxVolume = 0.0
        for line in cmdPipe.stdout:
            if 'max_volume' in line:
                db = line.split(':')[1]
                maxVolume = float(db.split('dB')[0].strip())
        
        cmdPipe.terminate()
        cmdPipe.stdout.close()
        cmdPipe.wait()
        
        return maxVolume

    def convertAVI(self):
        timer=time.time()

        bitRate = "1500k"
        if(self.outputHeight <= 64):
            bitRate = "300k"

        if self.blurEdit.get() == 1:
            filterGraph = self.getBlurFilterComplex(self.outputWidth, self.outputHeight)
            vidcommand = [ FFMPEG_BIN,
                '-i', self.inputFile,
                '-r', '%d' % (self.outputFrameRate),
                '-pix_fmt', 'yuv420p',
                '-filter_complex', filterGraph,
                '-map', '[vout]',
                '-map', '0:a?',
                '-b:v', bitRate,
                '-c:v', 'mjpeg',
                '-ac', '1',
                '-acodec', 'pcm_u8',
                '-af', 'volume=%.1fdB,aresample=%d,aresample=async=1000,aresample=osf=u8,asetnsamples=n=210:p=0' % (self.volumeAdjust, self.outputAudioSampleRate),
                '-y',
                self.outputFile]
        else:
            scaleCommand = self.getScaleCommand(self.outputWidth, self.outputHeight)
            vidcommand = [ FFMPEG_BIN,
                '-i', self.inputFile,
                '-r', '%d' % (self.outputFrameRate),
                '-pix_fmt', 'yuv420p',
                '-vf', scaleCommand,
                '-b:v', bitRate,
                '-c:v', 'mjpeg',
                '-ac', '1',
                '-acodec', 'pcm_u8',
                '-af', 'volume=%.1fdB,aresample=%d,aresample=async=1000,aresample=osf=u8,asetnsamples=n=210:p=0' % (self.volumeAdjust, self.outputAudioSampleRate),
                '-y',
                self.outputFile]
        
        print(vidcommand)
        vidPipe = '';
        if os.name=='nt' :
            startupinfo = sp.STARTUPINFO()
            startupinfo.dwFlags |= sp.STARTF_USESHOWWINDOW
            vidPipe=sp.Popen(vidcommand, stdout = sp.PIPE, stderr=sp.STDOUT, universal_newlines=True, startupinfo=startupinfo)
        else:
            vidPipe=sp.Popen(vidcommand, stdout = sp.PIPE, stderr=sp.STDOUT, universal_newlines=True)
        
        for line in vidPipe.stdout:
            if('time=' in line):
                finalTimeString=  line.split('time=')[1].split(' ')[0]
                finalTimeStringSplit=  finalTimeString.split(':')
                currentTime=0
                if len(finalTimeStringSplit) == 1:
                    currentTime = float(finalTimeStringSplit[0])
                if len(finalTimeStringSplit) == 2:
                    currentTime = float(finalTimeStringSplit[0])*60.0
                    currentTime += float(finalTimeStringSplit[1])
                if len(finalTimeStringSplit) == 3:
                    currentTime = float(finalTimeStringSplit[0])*60.0*60.0
                    currentTime = float(finalTimeStringSplit[1])*60.0
                    currentTime += float(finalTimeStringSplit[2])
                print(currentTime)
                self.progressBarVar.set(100.0*(currentTime/self.durationSeconds))

        
        
        #vidErr = vidPipe.stderr.read()
        vidFrame = vidPipe.stdout.read()
        print(len(vidFrame))
        while len(vidFrame)==self.inputVidFrameBytes:
            print(len(vidFrame))
            vidFrame = vidPipe.stdout.read(self.inputVidFrameBytes)
        
        
        self.progressBarVar.set(100.0)
        
        vidPipe.terminate()
        vidPipe.stdout.close()
        vidPipe.wait()

        timer=time.time()-timer

        m, s = divmod(timer, 60)
        h, m = divmod(m, 60)

        time.sleep(0.5)
        self.progressBarVar.set(0.0)
        #self.conversionTimeField.config(text = '%d:%02d:%02d' % ( h, m, s))

    def convertTSV(self):
        timer=time.time()
        
        output=open(self.outputFile, 'wb')
        devnull = open(os.devnull, 'wb')

        if self.blurEdit.get() == 1:
            filterGraph = self.getBlurFilterComplex(self.outputWidth, self.outputHeight)
            vidcommand = [ FFMPEG_BIN,
                '-i', self.inputFile,
                '-f', 'image2pipe',
                '-r', '%d' % (self.outputFrameRate),
                '-filter_complex', filterGraph,
                '-map', '[vout]',
                '-vcodec', 'rawvideo',
                '-pix_fmt', 'bgr565be',
                '-f', 'rawvideo', '-']
        else:
            scaleCommand = self.getScaleCommand(self.outputWidth, self.outputHeight)
            vidcommand = [ FFMPEG_BIN,
                '-i', self.inputFile,
                '-f', 'image2pipe',
                '-r', '%d' % (self.outputFrameRate),
                '-vf', scaleCommand,
                '-vcodec', 'rawvideo',
                '-pix_fmt', 'bgr565be',
                '-f', 'rawvideo', '-']
        
        vidPipe = '';
        if os.name=='nt' :
            startupinfo = sp.STARTUPINFO()
            startupinfo.dwFlags |= sp.STARTF_USESHOWWINDOW
            vidPipe=sp.Popen(vidcommand, stdin = sp.PIPE, stdout = sp.PIPE, stderr = devnull, bufsize=self.inputVidFrameBytes*10, startupinfo=startupinfo)
        else:
            vidPipe=sp.Popen(vidcommand, stdin = sp.PIPE, stdout = sp.PIPE, stderr = devnull, bufsize=self.inputVidFrameBytes*10)
            
        vidFrame = vidPipe.stdout.read(self.inputVidFrameBytes)
        
        audioCommand = [ FFMPEG_BIN,
            '-i', self.inputFile,
            '-f', 's16le',
            '-acodec', 'pcm_s16le',
            '-ar', '%d' % (self.outputAudioSampleRate),
            '-ac', '1',
            '-']
        
        audioPipe=''
        if (self.audioEnable.get() == 1):
            if os.name=='nt' :
                startupinfo = sp.STARTUPINFO()
                startupinfo.dwFlags |= sp.STARTF_USESHOWWINDOW
                audioPipe = sp.Popen(audioCommand, stdin = sp.PIPE, stdout=sp.PIPE, stderr = devnull, bufsize=self.audioFrameBytes*10, startupinfo=startupinfo)
            else:
                audioPipe = sp.Popen(audioCommand, stdin = sp.PIPE, stdout=sp.PIPE, stderr = devnull, bufsize=self.audioFrameBytes*10)
        
            audioFrame = audioPipe.stdout.read(self.audioFrameBytes)

        currentFrame=0;
        
        while len(vidFrame)==self.inputVidFrameBytes:
            currentFrame+=1
            if(currentFrame%30==0):
                self.progressBarVar.set(100.0*(currentFrame*1.0)/self.totalFrames)
            if (self.videoBitDepth.get() == 16):
                output.write(vidFrame)
            else:
                b16VidFrame=bytearray(vidFrame)
                b8VidFrame=[]
                for p in range(self.outputVidFrameBytes):
                    b8VidFrame.append(((b16VidFrame[(p*2)+0]>>0)&0xE0)|((b16VidFrame[(p*2)+0]<<2)&0x1C)|((b16VidFrame[(p*2)+1]>>3)&0x03))
                output.write(bytearray(b8VidFrame))
            
            vidFrame = vidPipe.stdout.read(self.inputVidFrameBytes)
            if (self.audioEnable.get() == 1):
                if len(audioFrame)==self.audioFrameBytes:
                    audioData=bytearray(audioFrame)
                    # This is slow
                    for j in range(int(round(self.audioFrameBytes/2))):
                        sample = ((audioData[(j*2)+1]<<8) | audioData[j*2]) + 0x8000
                        sample = (sample>>(16-self.tsvAudioSampleBitDepth)) & (0x0000FFFF>>(16-self.tsvAudioSampleBitDepth))
                        audioData[j*2] = sample & 0xFF
                        audioData[(j*2)+1] = sample>>8
                    
                    output.write(audioData)
                    audioFrame = audioPipe.stdout.read(self.audioFrameBytes)
                else:
                    emptySamples=[]
                    for samples in range(int(round(self.audioFrameBytes/2))):
                        emptySamples.append(0x00)
                        emptySamples.append(0x00)
                    output.write(bytearray(emptySamples))
        
        self.progressBarVar.set(100.0)
        
        vidPipe.terminate()
        vidPipe.stdout.close()
        vidPipe.wait()
        
        if (self.audioEnable.get() == 1):
            audioPipe.terminate()
            audioPipe.stdout.close()
            audioPipe.wait()

        output.close()

        timer=time.time()-timer

        m, s = divmod(timer, 60)
        h, m = divmod(m, 60)

        time.sleep(0.1)
        self.progressBarVar.set(0.0)
        #self.conversionTimeField.config(text = '%d:%02d:%02d' % ( h, m, s))

    def onBatchConvert(self):
        folder_path = tkinter.filedialog.askdirectory(title="Select folder containing video files")
        if not folder_path:
            return

        # Get supported video file extensions
        supported_exts = ('.mp4', '.mov', '.avi', '.gif', '.mpg', '.mpeg', '.mkv', '.wmv', '.flv', '.webm')
        
        # Find all video files in the folder
        video_files = []
        for filename in os.listdir(folder_path):
            if filename.lower().endswith(supported_exts):
                full_path = os.path.join(folder_path, filename)
                video_files.append(full_path)
        
        if not video_files:
            messagebox.showinfo('TinyTV Converter', 'No video files found in the selected folder!')
            return
        
        # Ask user for confirmation
        result = messagebox.askyesno('TinyTV Converter',
                                   f'Found {len(video_files)} video file(s) in the folder.\n\n'
                                   f'Convert all files using current settings?\n\n'
                                   f'Output format: {"TSV" if self.outputFormat.get() == 2 else "AVI"}\n'
                                   f'TV Type: {"TinyTV 2" if self.TVTypeOption.get() == 3 else "TinyTV Mini" if self.TVTypeOption.get() == 2 else "TinyTV DIY Kit"}\n'
                                   f'Blur Edit: {"On" if self.blurEdit.get() == 1 else "Off"}')
        
        if not result:
            return
        
        # Start batch conversion in a separate thread
        batch_thread = threading.Thread(target=self.batchConvertFiles, args=(video_files, folder_path))
        batch_thread.daemon = True
        batch_thread.start()

    def batchConvertFiles(self, video_files, output_folder):
        """Convert multiple video files using current settings"""
        total_files = len(video_files)
        converted_count = 0
        failed_count = 0
        
        for i, input_file in enumerate(video_files):
            try:
                # Update progress to show current file
                self.progressBarVar.set(0)
                
                # Set input file and get video info
                self.inputFile = input_file
                self.getVideoInfo(input_file)
                
                # Determine output file path and extension
                filename = os.path.splitext(os.path.basename(input_file))[0]
                if self.outputFormat.get() == 2:
                    output_ext = '.tsv'
                else:
                    output_ext = '.avi'
                
                output_file = os.path.join(output_folder, filename + output_ext)
                self.outputFile = output_file
                
                # Calculate volume adjustment if needed
                self.volumeAdjust = 0.0
                if self.normalizeAudio.get() == 1:
                    self.volumeAdjust = (0 - self.runVolumeDetect(True)) * 0.95
                
                # Convert the file
                if self.outputFormat.get() == 2:
                    self.convertTSV()
                else:
                    self.convertAVI()
                
                converted_count += 1
                print(f"Successfully converted: {os.path.basename(input_file)}")
                
            except Exception as e:
                failed_count += 1
                print(f"Failed to convert {os.path.basename(input_file)}: {str(e)}")
        
        # Show completion message
        message = f"Batch conversion complete!\n\n"
        message += f"Successfully converted: {converted_count} file(s)\n"
        if failed_count > 0:
            message += f"Failed: {failed_count} file(s)"
        
        # Use after() to show message from main thread
        self.after(0, lambda: messagebox.showinfo('TinyTV Converter', message))
        self.progressBarVar.set(0)

    def getVideoInfo(self, input_file):
        """Get video duration and other info without updating UI"""
        try:
            infoPipe = ''
            if os.name == 'nt':
                startupinfo = sp.STARTUPINFO()
                startupinfo.dwFlags |= sp.STARTF_USESHOWWINDOW
                infoPipe = sp.Popen([FFMPEG_BIN, "-i", input_file], 
                                   stdin=sp.PIPE, stdout=sp.DEVNULL, stderr=sp.PIPE, 
                                   bufsize=1000000, startupinfo=startupinfo)
            else:
                infoPipe = sp.Popen([FFMPEG_BIN, "-i", input_file], 
                                   stdin=sp.PIPE, stdout=sp.DEVNULL, stderr=sp.PIPE, 
                                   bufsize=1000000)
            
            info = infoPipe.stderr.read().decode('utf8')
            infoPipe.terminate()
            infoPipe.stderr.close()
            infoPipe.wait()
            
            if 'Invalid' in info:
                raise Exception("Invalid or unsupported file format")
            
            lines = info.splitlines()
            
            # Extract duration
            try:
                keyword = 'Duration: '
                line = [l for l in lines if keyword in l][0]
                match = re.findall("([0-9][0-9]:[0-9][0-9]:[0-9][0-9].[0-9][0-9])", line)[0]
                self.durationString = match[0:8]
                self.durationSeconds = float(match[0:2])*60.0*60.0 + float(match[3:5])*60.0 + float(match[6:11])
            except:
                # Handle GIFs or images that don't have duration
                if 'N/A' in info:
                    vidcommand = [FFMPEG_BIN, '-i', input_file, '-f', 'null', '-']
                    infoPipe = ''
                    if os.name == 'nt':
                        startupinfo = sp.STARTUPINFO()
                        startupinfo.dwFlags |= sp.STARTF_USESHOWWINDOW
                        infoPipe = sp.Popen(vidcommand, stdin=sp.PIPE, stdout=sp.PIPE, stderr=sp.PIPE, 
                                           bufsize=(self.outputWidth*2 * self.outputHeight*2)*3, startupinfo=startupinfo)
                    else:
                        infoPipe = sp.Popen(vidcommand, stdin=sp.PIPE, stdout=sp.PIPE, stderr=sp.PIPE, 
                                           bufsize=(self.outputWidth*2 * self.outputHeight*2)*3)
                    
                    info = infoPipe.stdout.readline()
                    infoerr = infoPipe.stderr.read().decode('utf8')
                    infoPipe.terminate()
                    infoPipe.stdout.close()
                    infoPipe.wait()
                    lines = infoerr.splitlines()
                    
                    try:
                        keyword = 'time='
                        line = [l for l in lines if keyword in l][0]
                        finalTimeString = line.split('time=')[1].split(' ')[0]
                        finalTimeStringSplit = finalTimeString.split(':')
                        if len(finalTimeStringSplit) == 1:
                            self.durationSeconds = float(finalTimeStringSplit[0])
                        elif len(finalTimeStringSplit) == 2:
                            self.durationSeconds = float(finalTimeStringSplit[0])*60.0 + float(finalTimeStringSplit[1])
                        elif len(finalTimeStringSplit) == 3:
                            self.durationSeconds = float(finalTimeStringSplit[0])*60.0*60.0 + float(finalTimeStringSplit[1])*60.0 + float(finalTimeStringSplit[2])
                        
                        hoursToDisplay = int(self.durationSeconds)/3600
                        minutesToDisplay = int(self.durationSeconds)/60
                        secondsToDisplay = int(self.durationSeconds)%60
                        if(hoursToDisplay == 0 and minutesToDisplay == 0 and secondsToDisplay == 0):
                            secondsToDisplay = 1
                        self.durationString = "%02d:%02d:%02d" % (hoursToDisplay, minutesToDisplay, secondsToDisplay)
                    except:
                        self.durationSeconds = 1.0/29.99
                        self.durationString = "--:--:--"
                else:
                    raise Exception("Could not determine video duration")
            
            # Calculate video data based on current settings
            self.calculateVideoData()
            
        except Exception as e:
            raise Exception(f"Error reading video file: {str(e)}")

def main():
    root = Tk()
    style = Style()
    TinyTVC = TinyTVConverter(root)
    #root.geometry("550x175+300+300")
    root.resizable(width=False, height=False)
    if os.name=='nt' :
        root.iconbitmap(resource_path('icon.ico'))
    #if sys.platform=='darwin':
        #root.iconbitmap(resource_path('icon.gif'))
        #root.iconphoto(True, PhotoImage(file="icon.gif"))
        #iconImage = Image("photo", file=resource_path('icon.gif'))
        #iconImage = Image('photo', file='icon.gif')
        #iconImage = PhotoImage(file='icon.gif')
        #root.tk.call('wm','iconphoto',root._w, iconImage)
        #Give up on title bar icon!
        #root.lift()
        #root.attributes('-topmost', True)
        #root.after_idle(root.attributes,'-topmost',False)
    #program_directory=sys.path[0]
    root.mainloop()

if __name__ == '__main__':
    main()  