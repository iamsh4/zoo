DiscJuggler v4.0 header:

   Size  Value
------------------------------------------------------ Number of sessions
    2    total_sessions
---------------------------------------------------------- Session header
    2    total_tracks (in current session)
------------------------------------------------------------ Track header
[   8    unkown data (not NULL)      3.00.780 only  (may not be present)]
    4    NULL
   20    0000 0100 0000 FFFF FFFF
         0000 0100 0000 FFFF FFFF    track start mark?
    4    discjuggler settings        no idea of internal bit fields
    1    filename_lenght
 [fl]    [filename]
    1    NULL
   10    NULL  (ISRC?)
    4    2   (always?)
    4    NULL
[   4    0x80000000 (4.x only)]
    4    max_cd_length = 0x514C8 (333000 dec) or 0x57E40 (360000 dec)
[   4    0x980000 (4.x only)]
-------------------------------------------- end track header (see below)
    2    2   (always?)
    4    pregap_length = 0x96 (150 dec) in sectors
    4    track_length (in sectors)
    6    NULL
    4    track_mode (0 = audio, 1 = mode1, 2 = mode2)
    4    NULL
    4    session_number (starting at 0)
    4    track_number (in current session, starting at 0)
    4    start_lba
    4    total_length (pregap+track), less if truncated
   16    NULL
    4    sector_size (0 = 2048, 1 = 2336, 2 = 2352)
    4    0 = audio, 4 = data
    1    NULL
    4    total_length (again?)
   20    NULL
    9    unknown data           3.0 only (build 780+: 00FFFFFFFFFFFFFFFF)
   78    unknown data      3.00.780 only (not NULL)

------------ start of new track; or if last one (on current session) then

    4    session_type (=track_type), sometimes 4 (=open session)
    4    NULL
    4    last session start lba
    1    NULL                   3.0 only
------------------------------------------------- next Session header, or

    2    NULL (total_tracks)  (means: no more sessions)

    [track header] again        3.0 only

    4    end_lba = start_lba + total_length of last track

    1    volumename_length
 [vl]    [volume name]
    1    NULL                   3.0 only
    4    0x01
    4    0x01
   14    NULL (UPC?)
   15    NULL
    4    image_version (see below)
    4    header_length (3.5+) or header_offset (previous versions)
  EOF

image_version:
  0x80000006 = v3.5.826+ and 4.0
  0x80000005 = v3.0 and 3.0.780
  0x80000004 = v2.0

All values in Little Endian unless specified.

2.0 is identified by image_version
3.0 and up is identified by image_version
3.0.780 is identified by signature 00FFFFFFFFFFFFFFFF (see above)
3.5 and up is identified by image_version
4.0 is identified by value 0x80000000 instead of max_cd_length
