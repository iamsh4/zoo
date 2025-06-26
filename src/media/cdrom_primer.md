# CDROM Primer
Please see https:problemkaputt.de/psx-spx.htm#cdromdiskformat

# Disc Hierarchy
```
session  one or more sessions per disc
track    1-99 track per disc
minutes  74+ minutes per disc
seconds  60 per minute
sectors  75 sectors per second
frame    98 frames per sector
bytes    33 bytes per frame (24 of user data + 1 subchannel + 8 ECC)
bits     14 bits per byte   (only 256 valid combinations)
```

# Sectors
The most common sector size for a physical disc is 2352 bytes:
```
2352 byte sector = 98 data frames * 24 user data bytes
```
How the sector data is interpreted is dependent on the "mode" of the track...

## Audio Sectors
2352 bytes of data arranged as `(LeftLsb,LeftMsb,RightLsb,RightMsb)`.
Each audio sample is 16bit stereo (32 bits for a single stereo sample), meaning
each sector contains 2352/4=588 samples. Since a sector is 1/75 of a second,
that's how we arrive at a CD Audio sample rate of 44.1 Khz.

## Mode 1 Sectors
This is how data is stored for PC/Dreamcast discs.

## Subchannel data
*Almost* no disc storage format supports storage of subchannel and ECC data for 
frames. We don't support any yet, and we may never need to, but they do exist.

**TODO**

## Ways of addressing a Sector
There are two schemes for refering to a location on the disc:

- mm:ss:ff (MSF) format, which specifies minutes:seconds:"frames"
  Note, here 'frames' means sectors.
- FAD/LAB/etc. absolute "frame address", here also refers to sectors,
  starting at the beginning of the disc.

Note, both of these refer to something called a "frame", but really they're 
describing a sector number. It's not possible to address something smaller than 
a sector, but you should be aware of the format described at the beginning. The 
upshot is, anywhere you see code or someone talking about discs and frames, 
they're talking about sectors.

# TODO: Pregap, Lead-in 

