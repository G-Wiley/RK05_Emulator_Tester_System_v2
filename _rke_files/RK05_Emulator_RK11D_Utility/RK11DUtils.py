import sys
import struct
import datetime
import argparse
import os

#
#   RK05 Emulator RK11D Utility, Version 1.1
#   Corresponds to RK05 Emulator File Format Version 1.1
#
#   Copyright 2024, Jay R. Jaeger
#
#   This file/program may be freely copied as desired, so long as the above
#   copyright notice is included.
#

#
#   I dislike Python (https://www.computercollection.net/index.php/2020/04/13/python-the-good-and-the-mostly-bad/)
#   So, why did I write this in Python?  Well, it is a little like FORTRAN and COBOL in the 1960s and 1970s:
#   Not necessarily the best tool, but it is functional, and widely available and implemented.  ;)
#

#
#   Define some constants

#
#   Preamble: bits from index to sync bit RK11-D Manual
#   Added two to match existing data that I saw
#   (Aka Bit times from sector pulse to start bit)
#  
# bitTimesToSyncBit = 0o15 * 2 * 8 + 2  
bitTimesToSyncBit = 221    # Determined empircally from output from emulator file save

#
#   Number of data words:  1 (cyl. addr) + 256 (data) + 1 (chksum) + 1 (postamble)
#   (Aka data bits after start bit)
#
dataWordsPerSector = 1 + 256 + 1 + 1
dataWordSize = 16

#
#   Various lengths in the header
#
hdrMagicLen = 10
hdrVersionLen = 4
hdrImageNameLen = 11
hdrDescLen = 200
hdrDateLen = 20
hdrControllerLen = 100

#
#   Other disk parameters
#
bitRate = 1440000
numberOfCylinders = 203
numberUserCylinders = 200
numberSectorsPerTrack = 12
numberOfHeads = 2
microsecondsPerSector = 3333 
magicNumber = "\x89RK05\r\n\x1A"
versionNumber = "1.1"
sectorSizeInWords = 256
wordSizeInBytes = 2
       

#
#   Function to write out a 4 byte integer MSB first
#

def writeIntMSB(fd, value):
    fd.write(struct.pack('>I',value))

#
#   Function to write out a 2 byte integer LSB first
#

def writeShortLSB(fd, value):
    fd.write(struct.pack('<H',value))

#
#   Function to read a two byte integer LSB first
#   Returns an UNSIGNED number, or -1 on error
#

def readShortLSB(fd):
    try:
        n = struct.unpack('<H',fd.read(2))[0]
        return n
    except Exception as inst:
        print("Error Reading a LSB 16 bit number: " + type(inst))
        print(inst.args)
        return(-1)
        
#
#   Function to read a four byte integer MSB first
#   Returns an UNSIGNED number, or -1 on error
#

def readIntMSB(fd):
    try:
        n = struct.unpack('>I',fd.read(4))[0]
        return n
    except Exception as inst:
        print("Error Reading a MSB 32 bit number: " + type(inst))
        print(inst.args)
        return(-1)

#
#   Function to take a string and turn it into bytes - WITHOUT ENCODING
#

def strToBytes(s):
    out = []
    for cp in s:
        out.append(struct.pack('B',ord(cp)))
    return b''.join(out)

#
#   Function to take a set of zero-terminated bytes of a given length
#   into a string with no decoding
#

def readCStringToString(fd,len):
    out = ""
    stop = False
    try:
        for i in range(0,len):
            b = int.from_bytes(fd.read(1),byteorder='little')
            if not stop:
                if b != 0:
                    out = out + chr(b)
                else:
                    stop = True                
        return out
    except Exception as inst:
        print("Error Reading a MSB 32 bit number: " + type(inst))
        print(inst.args)
        return(-1)

#
#   Parse the command line.  I finally found something to really like in Python...  ;)
#
argParser = argparse.ArgumentParser(description="RK05 Emulator RK11-D processing utility")
mainGroup = argParser.add_mutually_exclusive_group(required=True)
mainGroup.add_argument('-f','--format',help='Format a new emulator file',action='store_true')
mainGroup.add_argument('-s','--tosimh',help='Read an emulator file and write a SimH file',action='store_true')
mainGroup.add_argument('-e','--fromsimh',help='Read a SimH file and write an emulator file',action='store_true')
mainGroup.add_argument('-a','--analyze',help='Analyze an emulator file',action='store_true')
mainGroup.add_argument('-c','--compare',help='Compare two SimH images, noting any mismatched blocks',nargs=2)
argParser.add_argument('-d','--debug',help='Debug (specifying multiple times increases debug level)',action='count',default=0)
argParser.add_argument('-v','--verbose',help='Verbose (specifying multiple times increases verbosity)',action='count',default=0)    
argParser.add_argument('-i','--input',help="Input file name",nargs=1)
argParser.add_argument('-o','--output',help="Output file name",nargs=1)
argParser.add_argument('-n','--noclobber',help="Do NOT overwrite (clobber) existing output file",action='store_true')
argParser.add_argument('-p','--pattern',help='Pattern to use for format in hex, ct for count pattern (default 00)',nargs=1)
args = vars(argParser.parse_args())

#
#   If debugging, spit out what we saw for arguments
#
if args['debug']:
    print("Incoming arguments:")
    print(args)
    print()

debug = args['debug']
verbose = args['verbose']

#
#   Cross validate mutually inclusive options

if args['format'] and (args['output'] is None or args['input'] is not None):
    print("Usage: -f/--format requires -o/--output and NOT -i/--input")
    sys.exit(8)

if args['tosimh'] and (args['input'] is None or args['output'] is None):
    print("Usage: -s/--tosimh requires -i/--input and -o/--output")
    sys.exit(8)

if args['fromsimh'] and (args['input'] is None or args['output'] is None):
    print("Usage: -s/--tosimh requires -i/--input and -o/--output")
    sys.exit(8)

if args['analyze'] and (args['input'] is None or args['output'] is not None):
    print("Usage: -a/--analyze requires -i/--input and NOT -o/--output")
    sys.exit(8)

if args['pattern'] and not args['format'] and not args['fromsimh']:
    print("Usage: -p/--pattern only valid with -format")
    sys.exit(8)

#
#   If input file is required, open it
#
if args['input']:
    inFileName = args['input'][0]
    if not os.path.exists(inFileName):
        print("Error: Input file " + inFileName + " not found")
        sys.exit(8)
    infile = open(inFileName,"rb")

#
#   And, if an output file is required, open that
#
if args['output']:
    outFileName = args['output'][0]
    if os.path.exists(outFileName) and args['noclobber']:
        if input("Output file " + outFileName + " already exists.  Overwrite?" ).lower not in ["yes", "y"]:
            print("OK.  Exiting")
            sys.exit(4)
    outfile = open(outFileName,"wb")

if args['compare']:
    file1 = args['compare'][0]
    file2 = args['compare'][1]
    if not os.path.exists(file1):
        print("Error: Input file " + file1 + " not found")
        sys.exit(8)
    infile1 = open(file1,"rb")
    if not os.path.exists(file2):
        print("Error: Input file " + file2 + " not found")
        sys.exit(8)
    infile2= open(file2,"rb")


#
#   Initialize for error/abort handling
#
abort = False
error = False

#
#   If this is a compare, handle that right here
#
if args['compare']:
    for block in range(0, numberUserCylinders*numberSectorsPerTrack*numberOfHeads):
        inBuffer1 = infile1.read(sectorSizeInWords*wordSizeInBytes)
        inBuffer2 = infile2.read(sectorSizeInWords*wordSizeInBytes)
        if(inBuffer1 != inBuffer2):
            print("SimH file compare error block %d" %(block))
    infile1.close()
    infile2.close()
    exit(0)

#
#   If the function has an emulated input file, that drives things
#

if args['analyze'] or args['tosimh']:
    #   Read the magic number
    s = readCStringToString(infile,hdrMagicLen)
    if s != magicNumber:
        print("Input file Magic Number Mismatch: Expected /%s/ got /%s/" % (magicNumber,s,))
        if s == -1:
            abort = True  
    if not abort:
        s = readCStringToString(infile,hdrVersionLen)
        if s != versionNumber:
            print("Input file Version Number Mismatch: Expected /%s/ Got /%s/" % (versionNumber,s))
            if s == -1:
                abort = True
    if not abort:
        imageName = readCStringToString(infile,hdrImageNameLen)
        if imageName == -1:
            abort = True
        else: 
            if verbose:
                print("Incoming Image Name: " + imageName)
    if not abort:
        imageDescription = readCStringToString(infile,hdrDescLen)
        if imageDescription == -1:
            abort = True
        else:
            if verbose:
                print("Incoming Description: " + imageDescription)
    if not abort:
        imageDate = readCStringToString(infile,hdrDateLen)
        if imageDate == -1:
            abort = True
        else:
            if verbose:
                print("Incoming Image Date: " + imageDate)
    if not abort:
        imageController = readCStringToString(infile,hdrControllerLen)
        if imageController == -1:
            abort = True
        else:
            if verbose:
                print("Incoming Controller Name: " + imageController)
    if not abort:
        n = readIntMSB(infile)
        if n == -1:
            abort = True
        else:
            if verbose:
                print("Incoming Bit Rate: ",n)
            if n != bitRate:
                print("Unexpected bit rate: Expected ",bitRate,"got ",n)
                error = True
    if not abort:
        n = readIntMSB(infile)
        if n == -1:
            abort = True
        else:
            if verbose:
                print("Incoming Cylinders:",n)
            if n != numberOfCylinders:
                print("Unexpected number of Cylinders: Expected ",numberOfCylinders,"got ",n)
                error = True
    if not abort:
        n = readIntMSB(infile)
        if n == -1:
            abort = True
        else:
            if verbose:
                print("Incoming Sectors/Track:",n)
            if n != numberSectorsPerTrack:
                print("Unexpected sectors/track: Expected ",numberSectorsPerTrack,"got ",n)
                error = True
    if not abort:
        n = readIntMSB(infile)
        if n == -1:
            abort = True
        else:
            if verbose:
                print("Incoming Heads:",n)
            if n != numberOfHeads:
                print("Unexpected Number of Heads: Expected ",numberOfHeads,"got ",n)
                error = True
    if not abort:
        n = readIntMSB(infile)
        if n == -1:
            abort = True
        else:
            if verbose:
                print("Incoming Microseconds/Sector:",n)
            if abs(n-microsecondsPerSector) > microsecondsPerSector//10:
                print("Unexpected Microseconds/Sector: Expected ",microsecondsPerSector,"+/- 10%, got ",n)
                error = True

    if abort or error:
        infile.close()
        if args['tosimh']:
            outfile.close()
        sys.exit(8)
        
    if verbose:
        print("")

    for cyl in range(0,numberOfCylinders):
        for head in range(0,numberOfHeads):
            for sector in range(0,numberSectorsPerTrack): 

                chksum = 0           
                fileHead = 1-head   #  The file seems to be in reverse head order?

                #
                #   Process the two word LSB first emulator header, and
                #   check that the cylinder matches
                #
                if debug:
                    print("File offset is 0x%x" % infile.tell())
                bitTimesToSyncBit = readShortLSB(infile)
                bitsOfData = readShortLSB(infile)
                cylinderAddress = readShortLSB(infile) >> 5
                wordsOfData = (bitsOfData + 15 ) // 16
                bytesOfData = (bitsOfData + 3) // 8
                if verbose:
                    print("Sync Bits %d, Data Bits %d, Data Bytes: %d, Cylinder %d Head %d Sector %d" 
                          % (bitTimesToSyncBit,bitsOfData,bytesOfData,cylinderAddress,fileHead,sector))
                if cylinderAddress != cyl:
                    print("Cylinder Mismatch: Cylinder %d Head %d Sector %d, Header Cylinder %d" 
                            %(cyl, fileHead, sector, cylinderAddress))

                #
                #   Handle short blocks by reading in what is there and then extending the
                #   block to the appropriate size
                #                             
                if wordsOfData < sectorSizeInWords:
                    print("Short Sector: Cylinder %d Head %d Sector %d Sector size in Words: %d"                     
                          %(cyl,fileHead,sector,wordsOfData))
                    print("Extending short sector to normal size with 0x00")
                    dataBuffer = infile.read(bytesOfData)
                    dataBuffer = dataBuffer + b'\0'*(sectorSizeInWords*wordSizeInBytes - bytesOfData)
                    postamble = bytes(0)   # Empty bytes
                #
                #   Otherwise, it is a normal block so check its checksum too
                #
                else:
                    readLength = min(bytesOfData-2, sectorSizeInWords*wordSizeInBytes)
                    if debug:
                       print("Reading %d bytes of data." %(readLength))
                    dataBuffer = infile.read(readLength)  # Read all but the checksum
                    hdrChecksum = readShortLSB(infile)
                    for bp in range (0,len(dataBuffer),wordSizeInBytes):
                        chksum = (chksum + (dataBuffer[bp] + (dataBuffer[bp+1] << 8)) & 0xffff)
                    if chksum != hdrChecksum:
                        print("Checksum error: Cylinder %d Head %d Sector %d Header CRC %04x Calculated CRC %04x" 
                           %(cyl,fileHead,sector,hdrChecksum,chksum))
                    #
                    #   Read and throw away anything past the checksum (postamble zeros)
                    #
                    if readLength < bytesOfData - 4:
                        postamble = infile.read((bytesOfData-4) - readLength)

                #
                #   If we are in double verbose mode, dump the block data, including the postamble.
                #
                if verbose > 1:                
                    charData = "   *"    
                    for bp in range(0,len(dataBuffer)):
                        if bp % 16 == 0:
                            if bp > 0:
                                charData = charData + "*"
                                print(charData)
                            print("%04x:  " %(bp),end="")
                            charData = "   *"
                        print("%02x " %(dataBuffer[bp]),end="")
                        if dataBuffer[bp] >= 32 and dataBuffer[bp] < 128:
                            charData = charData + chr(dataBuffer[bp])
                        else:
                            charData = charData + "."
                    print("")   # Newline
                    if len(postamble) > 0:
                        print("Postamble: %d bytes" %(len(postamble)))
                    print("")   # Newline

                #
                #   If we are converting, write out the sector, but only up through
                #   the actual user data area of 200 cylinders (0 - 199)
                #
                
                if(args['tosimh'] and cyl < numberUserCylinders):
                    if(len(dataBuffer) != sectorSizeInWords*wordSizeInBytes):
                        print("Ooops: data buffer is of size %d" % (len(dataBuffer)))
                        #  The heads appear to be reversed in .rke files, so seek appropriately...
                    seeknum = (cyl * numberOfHeads * numberSectorsPerTrack * sectorSizeInWords * wordSizeInBytes) + (
                        fileHead * numberSectorsPerTrack * sectorSizeInWords * wordSizeInBytes) + (sector * sectorSizeInWords * wordSizeInBytes)
                    outfile.seek(seeknum)
                    outfile.write(dataBuffer)
                
    infile.close()
    if(args['tosimh']):
        outfile.close()

    sys.exit(0)

#
#   But if the function has an emulated output file, that drives th8ings,
#   whether it is a format to translating a simh file to an emulator image
#

if args['format'] or args['fromsimh']:

    #
    #   Prepare pattern for formatting if necessary, or for zero fill of the lst
    #   cylinders if converting from simH
    #

    tempBuffer = []
    for i in range(0,sectorSizeInWords*wordSizeInBytes):        
        if args['pattern'] and args['pattern'][0] == 'ct':
            tempBuffer.append(i & 0xff)            
        elif args['pattern'] and args['pattern'][0]:
            tempBuffer.append(int(args['pattern'][0],16) & 0xff)
        else:
            tempBuffer.append(0)
        
    fillBuffer = bytes(tempBuffer)

    #
    #   Prepare header data
    #

    hdrmagicNumber = "\x89RK05\r\n\x1A\x00\x00".ljust(hdrMagicLen,'\x00')
    hdrversionNumber = "1.1\x00".ljust(hdrVersionLen,'\x00')
    if args['format']:
        imageName = outFileName[0:10].ljust(11,'\x00')
    else:
        imageName = inFileName[0:10].ljust(11,'\x00')
    imageDescription = "GWiley RK05/RK11D Emulator image".ljust(200,'\x00')
    imageDate = datetime.datetime.today().strftime("%Y/%m/%d %H:%M:%S").ljust(20,'\x00')
    controllerName = "RK11-D".ljust(100,'\x00')

    #
    #   Write out the header
    #

    outfile.write(strToBytes(hdrmagicNumber))
    outfile.write(strToBytes(hdrversionNumber))
    outfile.write(strToBytes(imageName))
    outfile.write(strToBytes(imageDescription))
    outfile.write(strToBytes(imageDate))
    outfile.write(strToBytes(controllerName))
    writeIntMSB(outfile,bitRate)
    writeIntMSB(outfile,numberOfCylinders)
    writeIntMSB(outfile,numberSectorsPerTrack)
    writeIntMSB(outfile,numberOfHeads)
    writeIntMSB(outfile,microsecondsPerSector)

    #
    #   Write out the sectors, with a CRC of 0 in this trivial case.
    #

    for cyl in range(0,numberOfCylinders):
        for head in range(0,numberOfHeads):
            for sector in range(0,numberSectorsPerTrack):                
                chksum = 0
                fileHead = 1 - head   # File seems to have head 1 before head 0?
                writeShortLSB(outfile,bitTimesToSyncBit)
                writeShortLSB(outfile,dataWordsPerSector * dataWordSize) 
                writeShortLSB(outfile,cyl << 5)
                if args['format']:
                    outfile.write(fillBuffer)                    
                    for bp in range (0,len(fillBuffer),wordSizeInBytes):
                        chksum = (chksum + (fillBuffer[bp] + (fillBuffer[bp+1] << 8)) & 0xffff)
                    writeShortLSB(outfile,chksum)
                    outfile.write(bytes(2))     # 2 bytes of postamble
                else:    # fromSimH
                    if(cyl < numberUserCylinders):
                        # Seek to compensate for .rke files having head 1 first?
                        seeknum = (cyl * numberOfHeads * numberSectorsPerTrack * sectorSizeInWords * wordSizeInBytes) + (
                            (fileHead) * numberSectorsPerTrack * sectorSizeInWords * wordSizeInBytes) + (sector * sectorSizeInWords * wordSizeInBytes)
                        infile.seek(seeknum)
                        outBuffer = infile.read(sectorSizeInWords*wordSizeInBytes)
                    else:
                        outBuffer = fillBuffer
                    outfile.write(outBuffer)
                    for bp in range (0,len(outBuffer),wordSizeInBytes):
                        chksum = (chksum + (outBuffer[bp] + (outBuffer[bp+1] << 8)) & 0xffff)
                    writeShortLSB(outfile,chksum)
                    outfile.write(bytes(2))   # 2 bytes of postamble
                    
    if args['fromsimh']:
        infile.close()
    outfile.close()
    exit(0)



