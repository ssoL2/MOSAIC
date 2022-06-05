#!/usr/bin/env python3

# For Metadata Remover
import os
from tokenize import group
from PIL import Image
import PIL.ExifTags
from enum import Enum
import pathlib
import time
import shutil
import zipfile
import xml.etree.ElementTree as ET
from PyPDF2 import PdfFileReader, PdfFileWriter, PdfFileMerger
from PyPDF2.generic import NameObject, createStringObject
from setuptools import Command
# For Upload
from selenium import webdriver
from selenium.webdriver.chrome.service import Service
from webdriver_manager.chrome import ChromeDriverManager
from selenium.webdriver.common.by import By
import chromedriver_autoinstaller
from tkinter import filedialog
from tkinter import Tk
import psutil
chromedriver_autoinstaller.install()
# For Document Security
import re
from pdf2image import convert_from_path
import sys
import comtypes.client
import win32com.client as win32
from win32 import win32gui
import cv2
import numpy as np
import pytesseract
from pprint import pprint
import sqlite3 			# new added library, not in requirements.txt.

TempPdfPath = "C:/python/TempPdf/"
TempImgPath = "C:/python/TempImg/"
MaskImgPath = "C:/python/MaskImg/"
MaskFilPath = "C:/python/MaskFile/"
MetaFilPath = "C:/python/MetaFile/"
pytesseract.pytesseract.tesseract_cmd = r'C:\python\Tesseract\tesseract.exe'

# For Metadata Remover
class MetadataRemover_program:
    next_name: str
    ext_check = 0 

    def __init__(self, file_paths: list):
        """Constructor, checks if the image exists and if it has the valid extension and sets the image paths accordingly.
        Skips all commands"""

        # Image
        self.img_possible_extensions = [".jpeg", ".jpg", ".png", ".gif", ".bmp", ".tiff"]
        # OLE File
        self.ole_possible_extensions = [".xlsx", ".xls", ".pptx", ".ppt", ".docx", ".doc"]
        # Movie
        self.video_possible_extensions = [".avi", ".mp4", ".m4a", ".mkv", ".mov", ".wmv", ".mov", ".webm", ".ogv", ".flv", ".vob", ".ogg", ".3gp"]
        # Other File (hwp는 MS OLE와 다르기 때문에 Other에 분류함) => pdf (hwp는 일단 제외)
        self.pdf_possible_extensions = [".pdf"]
        self.rename_files = True
        self.file_paths = file_paths

        self.next_name = ""
        temp = []

        if file_paths is None:
            raise TypeError("Argument must be a list of strings")
        if len(file_paths) == 0:
            raise ValueError("Argument must have at least one file specified")
        if not isinstance(file_paths[0], str):
            raise TypeError("Argument must be a list of strings")
        if len(file_paths) == 1 and (file_paths[0]) == Command.NORENAME.value:
            raise ValueError("Argument must have at least one file specified")

        if self.path_exists(file_paths):
            path = pathlib.Path(file_paths)
            extension = path.suffixes[0]
            if extension in self.img_possible_extensions:
                self.ext_check = 1 # Image
                temp.append(file_paths)
            elif extension in self.ole_possible_extensions:
                self.ext_check = 2 # OLE File
                temp.append(file_paths)
            elif extension in self.video_possible_extensions:
                self.ext_check = 3 # MOVIE
                temp.append(file_paths)
            elif extension in self.pdf_possible_extensions:
                self.ext_check = 4 # Other File
                temp.append(file_paths)                
            
            if not self.ext_check:
                raise ValueError("File extension not supported")

        else:
            raise FileNotFoundError(f"File does not exist")

        if len(temp) > 0:
            self.file_paths = temp


    def clean_files(self):
        for path in self.file_paths:
            if self.ext_check == 1:
                #Image
                self.img_clear_metadata(path)
            if self.ext_check == 2:
                #Image
                self.ole_clear_metadata(path)
            if self.ext_check == 3:
                #Image
                self.video_clear_metadata(path)
            if self.ext_check == 4:
                #Image
                self.pdf_clear_metadata(path)


    @staticmethod
    def path_exists(path: str) -> bool:
        return os.path.exists(path)


    def img_clear_metadata(self, path_of_file_to_clean):
        """Make a copy of the image without the metadata from the original."""
        image = PIL.Image.open(path_of_file_to_clean)
        data = list(image.getdata())
        image_without_exif = PIL.Image.new(image.mode, image.size)
        image_without_exif.putdata(data)

        self.next_name = (path_of_file_to_clean.split("/")[-1])
        try:
            self.__createFolder(MetaFilPath)
            image_without_exif.save(
                MetaFilPath + self.next_name if \
                    len(os.path.dirname(path_of_file_to_clean)) > 0 else self.next_name
                if self.rename_files else path_of_file_to_clean
            )
            #os.remove(path_of_file_to_clean)
        except PermissionError:
            print("Permission denied")


    def ole_clear_metadata(self, path_of_file_to_clean):

        self.next_name = (path_of_file_to_clean.split("/")[-1])
        try:
            zipfile.ZipFile(path_of_file_to_clean).extractall(MetaFilPath + "zip/")
            app_xml = r"C:\\python\\MetaFile\\zip\\docProps\\app.xml" # MetaFilPath 1
            core_xml = r"C:\\python\\MetaFile\\zip\\docProps\\core.xml" # MetaFilPath 2
           
           # app_xml
            with open(app_xml, 'rt', encoding='UTF8') as f:
                data = f.read()  
                # 정규표현식 도전
                m = re.search('<Properties(.+?)>(.+?)</Properties>', data)
                data_fix = re.sub(m.group(2), '', data)
                if not m:
                    print('not regulation m !!')
                #print(data_fix)
            with open(app_xml, 'wt', encoding='UTF8') as f:
                f.write(data_fix)
                
            # core_xml
            with open(core_xml, 'rt', encoding='UTF8') as f:
                data = f.read()  
                # 정규표현식 도전
                m = re.search('<cp:coreProperties(.+?)>(.+?)</cp:coreProperties>', data)
                data_fix = re.sub(m.group(2), '', data)
                if not m:
                    print('not regulation m !!')
                #print(data_fix)
            with open(core_xml, 'wt', encoding='UTF8') as f:
                f.write(data_fix)   
                
            # 재압축
            shutil.make_archive(MetaFilPath + self.__get_new_file_name(), "zip", "C:\\python\\MetaFile\\zip") # MetaFilPath 3
            os.rename(MetaFilPath + self.__get_new_file_name() + ".zip", MetaFilPath + self.next_name)
            shutil.rmtree("C:\\python\\MetaFile\\zip") # MetaFilPath 4
                
        except PermissionError:
            print("Permission denied")  
              
    
    def video_clear_metadata(self, path_of_file_to_clean):

        self.next_name = (path_of_file_to_clean.split("/")[-1])
        try:
            # [필수] ffmpeg.exe가 필요함!
            # [주의] window command 이므로, String 양끝에 "" 필수로 넣어줘야 Space Bar 인식함. 
            os.system('ffmpeg -i "' + path_of_file_to_clean + '" -map_metadata -1 -c:v copy -c:a copy ' + MetaFilPath + self.next_name + '"')
        except PermissionError:
            print("Permission denied")
            

    def pdf_clear_metadata(self, path_of_file_to_clean):

        self.next_name = (path_of_file_to_clean.split("/")[-1])
        try:
            with open(path_of_file_to_clean, 'rb') as f1:
                pdf_in = PdfFileReader(f1)
                writer = PdfFileWriter()

                for page in range(pdf_in.getNumPages()):
                    writer.addPage(pdf_in.getPage(page))
                
                infoDict = writer._info.getObject()
                
                info = pdf_in.documentInfo
                for key in info:
                    infoDict.update({NameObject(key): createStringObject(info[key])})

                list_of_data_to_delete = ['/CreationDate','/Author','/Creator','/ModDate','/Producer','/Title']
                for item in list_of_data_to_delete:
                    try:
                        infoDict.update({NameObject(item): createStringObject(u'')})
                    except:
                        print("can't delete : ",item)

                with open(MetaFilPath + self.next_name, 'wb') as f2:
                    writer.write(f2)

        except PermissionError:
            print("Permission denied")


    def __get_new_file_name(self):
        """Returns a day-time name."""
        timestr = time.strftime("%Y%m%d-%H%M%S")
        next_name = "metadataRemover_" + timestr
        return next_name


    def __sizeof__(self) -> int:
        return len(self.file_paths)
    

    def __createFolder(self, directory):
        try:
            if not os.path.exists(directory):
                os.makedirs(directory)
        except OSError:
            print ('Error: Creating directory. ' +  directory)

# For Document Security
class DocumentSecurity_program:

    def __init__(self, file_paths: list):

        # PDF
        self.pdf_possible_extensions = [".pdf"]
        # Word File
        self.doc_possible_extensions = [".docx", ".doc"]
        # hwp
        self.hwp_possible_extensions = [".hwp", ".hwpx"]
        # Image
        self.image_possible_extensions = [".jpeg", ".jpg", ".png"]
        # Ppt
        #self.ppt_possible_extensions = [".pptx"]
        self.rename_files = True
        self.file_paths = file_paths

        self.next_name = ""
        temp = []

        if file_paths is None:
            raise TypeError("Argument must be a list of strings")
        if len(file_paths) == 0:
            raise ValueError("Argument must have at least one file specified")
        if not isinstance(file_paths[0], str):
            raise TypeError("Argument must be a list of strings")
        if len(file_paths) == 1 and (file_paths[0]) == Command.NORENAME.value:
            raise ValueError("Argument must have at least one file specified")

        if self.path_exists(file_paths):
            path = pathlib.Path(file_paths)
            extension = path.suffixes[0]
            if extension in self.pdf_possible_extensions:
                self.ext_check = 1 # PDF
                temp.append(file_paths)
            elif extension in self.doc_possible_extensions:
                self.ext_check = 2 # Word File
                temp.append(file_paths)
            elif extension in self.hwp_possible_extensions:
                self.ext_check = 3 # hwp
                temp.append(file_paths)
            elif extension in self.image_possible_extensions:
                self.ext_check = 4 # Image
                temp.append(file_paths)     
            #elif extension in self.ppt_possible_extensions:
            #    self.ext_check = 5 # Ppt
            #    temp.append(file_paths)              
            
            if not self.ext_check:
                raise ValueError("File extension not supported")

        else:
            raise FileNotFoundError(f"File does not exist")

        if len(temp) > 0:
            self.file_paths = temp

    def clean_files(self):
        for path in self.file_paths:
            if self.ext_check == 1:
                # PDF
                self.pdf_clear_Personalinfo(path)
            if self.ext_check == 2:
                # Word
                self.doc_clear_Personalinfo(path)
            if self.ext_check == 3:
                # hwp
                self.hwp_clear_Personalinfo(path)
            if self.ext_check == 4:
                # Image
                self.img_clear_Personalinfo(path)
            #if self.ext_check == 5:
                # Ppt
            #    self.ppt_clear_Personalinfo(path)

    @staticmethod
    def path_exists(path: str) -> bool:
        return os.path.exists(path)
    
    # 파일 형식 별 문서보안 처리 시작

    def img_clear_Personalinfo(self, path_of_file_to_clean):
        # img to maskimg
        maskimg = Masking_Personalinfo(path_of_file_to_clean)
        # extract filename
        self.next_name = (path_of_file_to_clean.split("/")[-1])
        # save as final path + filename
        cv2.imwrite(MaskFilPath + (path_of_file_to_clean.split("/")[-1]), maskimg)
        # delete maskimg path
        DeleteAllFiles(MaskImgPath)

        #return maskimg

    def pdf_clear_Personalinfo(self, path_of_file_to_clean):
        # pdf to imgs
        pages = convert_from_path(path_of_file_to_clean, poppler_path='C:\\python\\poppler-22.04.0\\Library\\bin')
        p = 0
        for i, page in enumerate(pages): 
            page.save(TempImgPath + str(i) + ".jpg", "JPEG")
            # img to maskimg
            maskimg = Masking_Personalinfo((TempImgPath + str(i) + ".jpg"))
            # maskimg to pdf
            mimg = Image.open((MaskImgPath + str(i) + ".jpg"))
            pdf = mimg.convert('RGB')
            pdf.save(MaskImgPath + str(i) + ".pdf")
            p += 1
        # merge pdf
        pdfmergepath(p, path_of_file_to_clean)

        # delete maskimg path, delete tempimg path
        DeleteAllFiles(TempImgPath)
        DeleteAllFiles(MaskImgPath)

        #return (MaskFilPath + (path_of_file_to_clean.split("/")[-1]).split(".")[0] + ".pdf")

    def doc_clear_Personalinfo(self, path_of_file_to_clean):
        wdFormatPDF = 17

        inputFile = os.path.abspath(path_of_file_to_clean)
        outputFile = os.path.abspath(TempPdfPath + (path_of_file_to_clean.split("/")[-1]).split(".")[0] + ".pdf")
        print(TempPdfPath + (path_of_file_to_clean.split("/")[-1]).split(".")[0] + ".pdf")
        file = open(outputFile, "w")
        file.close()
        word = win32.Dispatch('Word.Application')
        doc = word.Documents.Open(inputFile)
        doc.SaveAs(outputFile, FileFormat=wdFormatPDF)
        doc.Close()
        word.Quit()

        # pdf to imgs
        pages = convert_from_path(TempPdfPath + (path_of_file_to_clean.split("/")[-1]).split(".")[0] + ".pdf", poppler_path='C:\\python\\poppler-22.04.0\\Library\\bin')
        p = 0
        for i, page in enumerate(pages): 
            page.save(TempImgPath + str(i) + ".jpg", "JPEG")
            # img to maskimg
            maskimg = Masking_Personalinfo((TempImgPath + str(i) + ".jpg"))
            # maskimg to pdf
            mimg = Image.open((MaskImgPath + str(i) + ".jpg"))
            pdf = mimg.convert('RGB')
            pdf.save(MaskImgPath + str(i) + ".pdf")
            p += 1
        # merge pdf
        pdfmergepath(p, path_of_file_to_clean)

        # delete maskimg path, delete tempimg path
        DeleteAllFiles(TempImgPath)
        DeleteAllFiles(MaskImgPath)

        #return (MaskFilPath + (path_of_file_to_clean.split("/")[-1]).split(".")[0] + ".pdf")

        # doc to doc, non convert version

    def hwp_clear_Personalinfo(self, path_of_file_to_clean):
        # load
        hwp = win32.gencache.EnsureDispatch("HWPFrame.HwpObject")
        # hiding
        hwnd = win32gui.FindWindow(None, 'Noname 1 - HWP')
        #print(hwnd)
        #win32gui.ShowWindow(hwnd, 0)
        hwp.RegisterModule('FilePathCheckDLL', 'FilePathCherckerModule')

        hwp.Open(path_of_file_to_clean)
        hwp.SaveAs(TempPdfPath + (path_of_file_to_clean.split("/")[-1]).split(".")[0] + ".pdf", "PDF")
        #win32gui.ShowWindow(hwnd, 5)
        hwp.Quit()

        # pdf to imgs
        pages = convert_from_path(TempPdfPath + (path_of_file_to_clean.split("/")[-1]).split(".")[0] + ".pdf" , poppler_path='C:\\python\\poppler-22.04.0\\Library\\bin')
        p = 0
        for i, page in enumerate(pages): 
            page.save(TempImgPath + str(i) + ".jpg", "JPEG")
            # img to maskimg
            maskimg = Masking_Personalinfo((TempImgPath + str(i) + ".jpg"))
            # maskimg to pdf
            mimg = Image.open((MaskImgPath + str(i) + ".jpg"))
            pdf = mimg.convert('RGB')
            pdf.save(MaskImgPath + str(i) + ".pdf")
            p += 1
        # merge pdf
        pdfmergepath(p, path_of_file_to_clean)

        # delete maskimg path, delete tempimg path
        DeleteAllFiles(TempImgPath)
        DeleteAllFiles(MaskImgPath)

        #return (MaskFilPath + (path_of_file_to_clean.split("/")[-1]).split(".")[0] + ".pdf")

    def ppt_clear_Personalinfo(self, path_of_file_to_clean):
        
        powerpoint = comtypes.client.CreateObject("Powerpoint.Application")
        powerpoint.Visible = True
        slides = powerpoint.Presentations.Open(path_of_file_to_clean)
    
        file_name = (path_of_file_to_clean.split("/")[-1]).split(".")[0]
        output_file_path = os.path.join(TempPdfPath, file_name + ".pdf")
    
        slides.SaveAs(output_file_path, FileFormat=32)
        slides.Close()

        # pdf to imgs
        pages = convert_from_path(TempPdfPath + (path_of_file_to_clean.split("/")[-1]).split(".")[0] + ".pdf", poppler_path='C:\\python\\poppler-22.04.0\\Library\\bin')
        p = 0
        for i, page in enumerate(pages): 
            page.save(TempImgPath + str(i) + ".jpg", "JPEG")
            # img to maskimg
            maskimg = Masking_Personalinfo((TempImgPath + str(i) + ".jpg"))
            # maskimg to pdf
            mimg = Image.open((MaskImgPath + str(i) + ".jpg"))
            pdf = mimg.convert('RGB')
            pdf.save(MaskImgPath + str(i) + ".pdf")
            p += 1
        # merge pdf
        pdfmergepath(p, path_of_file_to_clean)

        # delete maskimg path, delete tempimg path
        DeleteAllFiles(TempImgPath)
        DeleteAllFiles(MaskImgPath)

        #return (MaskFilPath + (path_of_file_to_clean.split("/")[-1]).split(".")[0] + ".pdf")

    def __get_new_file_name(self):
        """Returns a day-time name."""
        timestr = time.strftime("%Y%m%d-%H%M%S")
        next_name = "DocumentSecurity_" + timestr
        return next_name

    def __sizeof__(self) -> int:
        return len(self.file_paths)

    def __createFolder(self, directory):
        try:
            if not os.path.exists(directory):
                os.makedirs(directory)
        except OSError:
            print ('Error: Creating directory. ' +  directory)

class pi_algo:

    regex_list = list()
    userdefined_exclude_list = list()
    userdefined_include_list = list()

    def __init__(self, db_name):
        self.db_name = db_name

    def get_db(self):
        try:
            conn = sqlite3.connect(self.db_name, isolation_level=None)
        except:
            sys.stderr.write("No file: %s\n" % self.db_name)
            exit(1)
        c = conn.cursor()
        c.execute("SELECT regex FROM regex")
        rows = c.fetchall()
        rows = [list(rows[x]) for x in range(len(rows))]
        for i in range(len(rows)):
            pi_algo.regex_list.append(rows[i][0])
        c.execute("SELECT * FROM userdefined_include")
        rows = c.fetchall()
        rows = [list(rows[x]) for x in range(len(rows))]
        for i in range(len(rows)):
            pi_algo.userdefined_include_list.append(rows[i][0])
        c.execute("SELECT * FROM userdefined_exclude")
        rows = c.fetchall()
        rows = [list(rows[x]) for x in range(len(rows))]
        for i in range(len(rows)):
            pi_algo.userdefined_exclude_list.append(rows[i][0])
        conn.close()

    def check(self, word):
        for include in pi_algo.userdefined_include_list:
            if word == include:
                print(word + " 포함")
                return True
        for exclude in pi_algo.userdefined_exclude_list:
            if word == exclude:
                print(word + " 제외")
                return False
        for regex in pi_algo.regex_list:
            if re.compile(regex).search(word):
                print(word + " 개인정보")
                return True
        return False

def pdfmergepath(p, path_of_file_to_clean):
    merger = PdfFileMerger()
    for i in range(p):
        merger.append((MaskImgPath + str(i) + ".pdf"))
    merger.write((MaskFilPath + (path_of_file_to_clean.split("/")[-1]).split(".")[0] + ".pdf"))
    merger.close()

# For Document Security
def Masking_Personalinfo(path_of_file_to_clean):      
    large = cv2.imread(path_of_file_to_clean) 
    rgb = cv2.pyrDown(large)
    small = cv2.cvtColor(rgb, cv2.COLOR_BGR2GRAY)
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
    grad = cv2.morphologyEx(small, cv2.MORPH_GRADIENT, kernel)
    _, bw = cv2.threshold(grad, 0.0, 255.0, cv2.THRESH_BINARY | cv2.THRESH_OTSU)
    kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (9, 1))
    connected = cv2.morphologyEx(bw, cv2.MORPH_CLOSE, kernel)
    # using RETR_EXTERNAL instead of RETR_CCOMP
    contours, hierarchy = cv2.findContours(connected.copy(), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    mask = np.zeros(bw.shape, dtype=np.uint8)

    config = ('-l kor+eng --oem 3 --psm 11') #

    a = pi_algo("privacy_discrimination.db")
    a.get_db()

    for idx in range(len(contours)):
        x, y, w, h = cv2.boundingRect(contours[idx])
        mask[y:y+h, x:x+w] = 0
        cv2.drawContours(mask, contours, idx, (255, 255, 255), -1)
        r = float(cv2.countNonZero(mask[y:y+h, x:x+w])) / (w * h)
        if r > 0.45 and w > 8 and h > 8:
            ##cv2.rectangle(rgb, (x, y), (x+w-1, y+h-1), (0, 255, 0), 1)
            cropped = rgb[y:y + h, x:x + w]
            text = pytesseract.image_to_string(cv2.cvtColor(cropped, cv2.COLOR_BGR2RGB),config=config)
            #text = text.replace(" ","")
            print("---------")
            print(text)
            #for i in regexp:
            #    if regexp[i].search(text): 
            #        cnt = 1
            input_text = text.split()
            for i in input_text:
                print(i)
                if a.check(i) :
                    to_mosaic(rgb, contours[idx])
                    #print("check!")

        cv2.waitKey()

    cv2.imwrite(MaskImgPath + (path_of_file_to_clean.split("/")[-1]), rgb)

    return rgb

def DeleteAllFiles(filePath):
    if os.path.exists(filePath):
        for file in os.scandir(filePath):
            os.remove(file.path)
        return 'Remove All File'
    else:
        return 'Directory Not Found'

def mosaic(src, ratio=0.1):
    small = cv2.resize(src, None, fx=ratio, fy=ratio, interpolation=cv2.INTER_NEAREST)
    return cv2.resize(small, src.shape[:2][::-1], interpolation=cv2.INTER_NEAREST)
def to_mosaic(img, pts):
    (x, y, w, h) = cv2.boundingRect(pts)
    img[y:y + h, x:x + w] = mosaic(img[y:y + h, x:x + w], ratio=0.1)

# For Upload, unused/temporary function
#def cropshot() :
    
# For Upload, driver setting
def set_chrome_driver():
    # Check if chrome driver is installed or not
    chrome_ver = chromedriver_autoinstaller.get_chrome_version().split('.')[0]
    driver_path = f'./{chrome_ver}/chromedriver.exe'
    if os.path.exists(driver_path):
        print(f"chrome driver is insatlled: {driver_path}")
    else:
        print(f"install the chrome driver(ver: {chrome_ver})")
        chromedriver_autoinstaller.install(True)

    chrome_options = webdriver.ChromeOptions()
    #chrome_options.add_experimental_option("excludeSwitches", ["enable-logging"])
    chrome_options.add_experimental_option('debuggerAddress', '127.0.0.1:9222')
    driver = webdriver.Chrome(service=Service(ChromeDriverManager().install()), options=chrome_options)
    return driver

# For Upload Security
if __name__ == "__main__":
    # For Upload, unused/temporary function
    driver = set_chrome_driver()
    print("===== Chrome Driver Setting is completed. =====\n")
    process_name = "chrome" # chromium browser ex)microsoft edge
    pid = None

    while True:
        # automatic detection to automatic upload
        for proc in psutil.process_iter():
            if process_name in proc.name():
                for dll in proc.memory_maps():
                    if 'Windows.UI.FileExplorer.dll' in dll.path:

                        # explorer process kill first
                        print("pid of process is " + str(proc.pid))
                        print("detected process is " + proc.name())
                        print("detected dll is " + dll.path)
                        proc.kill()
                        print("===== kill process complete =====")

                        # open new file explorer 
                        root = Tk()
                        root.geometry("400x1")
                        root.title("파일 변환 중... 잠시만 기다려주세요.")
                        #root.withdraw()
                        root.filename = filedialog.askopenfilename(initialdir = "C:/Images", title = "열기", filetypes = (("all files", "*.*"),("png files","*.png")), multiple = True)
                        #root.filename = filedialog.askopenfilename(initialdir = "E:/Images", title = "열기", filetypes = (("all files", "*.*"),("png files","*.png")))
                        #print (root.filename)

                        # Current page check
                        driver.switch_to.window(driver.window_handles[0])
                        #URL = driver.current_url
                        #print("current URL is " + URL)

                        # for DocumentSecurity_program upload
                        
                        # 3. for Both
                        for i in root.filename:
                            print("Input : " + i)
                            DS = DocumentSecurity_program(i)
                            DS.rename_files = False
                            DS.clean_files()
                            extension = (i.split("/")[-1]).split(".")[-1]
                            if extension in ["jpeg", "jpg", "png", "pdf"]:
                                target = (MaskFilPath + (i.split("/")[-1]))
                                print("personal data of " + MaskFilPath + (i.split("/")[-1]) + " deleted.")
                                MR = MetadataRemover_program(target)
                                MR.rename_files = False
                                MR.clean_files()
                                newtarget = (MetaFilPath + (i.split("/")[-1]))
                                sourcecode_elem = driver.find_element(By.XPATH, value="//input[@type='file']")
                                sourcecode_elem.send_keys(newtarget)                            
                                print("Upload : " + newtarget)
                            else:
                                target = (MaskFilPath + (i.split("/")[-1]).split(".")[0] + ".pdf")
                                print("personal data of " + MaskFilPath + (i.split("/")[-1]) + " deleted.")
                                MR = MetadataRemover_program(target)
                                MR.rename_files = False
                                MR.clean_files()
                                newtarget = (MetaFilPath + (i.split("/")[-1]).split(".")[0] + ".pdf")
                                sourcecode_elem = driver.find_element(By.XPATH, value="//input[@type='file']")
                                sourcecode_elem.send_keys(newtarget)                            
                                print("Upload : " + newtarget)
                        
                        # Finish
                        root.destroy()
                        print("===== Upload completed =====\n")
                        continue

        # normal chromium browser + user trigger version (unused), change to chrome driver + automatic detection
        #if keyboard.is_pressed("shift + q") :
        #    URL = cropshot()
        #    driver.get(URL) 
        # ...

        # in DesktopApp, ON/OFF setting
        #if keyboard.read_key() == "esc":
        #    break